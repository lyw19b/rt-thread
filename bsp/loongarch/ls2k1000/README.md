# LS2K1000LA 与 LS2K0300

本 BSP 面向 LoongArch 版龙芯 2K1000LA，默认使用双核 SMP RT-Thread 标准版。板级地址和外设连接参考 `rt-thread-2k1000/bsp/loongarch/ls2kdev`，网络驱动已重新组织实现。Nano、Smart 的 QEMU 配置仍在 `bsp/qemu-virt64-loongarch`；本板提供 `smart_defconfig`，已实现高 DDR 的 Smart 地址空间初始化。

## 构建

```sh
cd bsp/loongarch/ls2k1000
export RTT_EXEC_PATH="$HOME/local/loongarch64-linux-musl-gcc-15.2.0/bin"
cp network_defconfig .config  # 双网口、lwIP 2.1.2、DHCP、ping、SAL sockets
scons --pyconfig-silent
scons -j8
```

`standard_defconfig` 保留不启用网络的基础配置。两种配置均开启 SMP、默认 2 核，可在 menuconfig 中改为 1 核或关闭 SMP，均不依赖外部软件包。用 `scons --menuconfig` 调整配置，再由 SCons 生成 `rtconfig.h`。

默认生成 ELF 和原始镜像。需要 U-Boot 镜像时设置 `MKIMAGE` 为支持 LoongArch 的 mkimage 路径。内核链接地址为 `0x9000000098000000`；固件需完成 DDR、外设时钟、引脚复用和 PCI BAR 分配。本仓库不保存固件、工具链或生成的二进制。

## 网卡配置

| 配置 | 设备名 | PCI 配置空间物理地址 | LIOINTC IRQ |
| --- | --- | --- | --- |
| `BSP_USING_GMAC0` | e0 | `0xfe00001800` | 12 |
| `BSP_USING_GMAC1` | e1 | `0xfe00001900` | 14 |

在启用 lwIP 且保留 RX 工作线程时，可以选择 `BSP_USING_GMAC`。每个网口有独立的描述符环、锁、PHY 状态和统计信息。驱动读取固件分配的 BAR0，检查地址并启用内存访问、总线主控和传统中断；不会重新枚举或分配 PCI 资源。

- `BSP_GMAC0_PHY` / `BSP_GMAC1_PHY`：PHY 地址，默认 `-1` 扫描 MDIO 总线。板上存在多个 PHY 时应明确设置。
- `BSP_GMAC0_MAC` / `BSP_GMAC1_MAC`：可填 `02:xx:xx:xx:xx:xx` 格式的单播 MAC。空字符串读取固件地址；无有效地址时使用本地地址并打印提示。多块板联网前需配置唯一地址。
- `BSP_GMAC_MDIO_CLOCK`：MDIO 时钟分频选择，默认 1 对应 100～150 MHz CSR 时钟。需与固件实际时钟一致。
- `BSP_GMAC_RING_SIZE`：每个收发环的描述符数，默认 32，范围 4～128。
- `RT_LWIP_ETH_PAD_SIZE`：以太网头前的内存填充，默认 0，也可设为 2 以对齐 IP 头；填充不会发送到线路。

PHY 使用 Clause 22 MDIO，支持自动协商和链路轮询。驱动保留固件设置的 PHY 厂商寄存器，包括板级 RGMII 延迟；这些设置需在实板确认。MDIO 和 DMA 复位均有超时，初始化失败不会继续启动 DMA。

## 实现边界

- `bsp/loongarch/common/gmac/gmac.h`：寄存器和描述符定义、核心接口。
- `bsp/loongarch/common/gmac/gmac.c`：MDIO、PHY、MAC 初始化、描述符所有权和环索引，不依赖 lwIP。
- `bsp/loongarch/common/gmac/drv_gmac.c`：RT-Thread 设备注册、pbuf 转换、同步、中断通知、链路检查和总线错误恢复。
- `bsp/loongarch/common/ls2k_gmac_platform.h`：板级 GMAC 资源和 DMA 地址范围接口。
- `drivers/drv_gmac_platform.c`：板级 GMAC 资源获取与 DMA 地址范围。
- `drivers/drv_pci.c`：2K1000LA 的 BAR 检查和 PCI 命令。
- `libcpu/loongarch/mach/common/platform_interrupt.c`：两款芯片共用的传统 LIOINTC 驱动，包含边沿确认、屏蔽和分发。
- `bsp/loongarch/common/drv_uart.c`：共用串口驱动，Nano 使用轮询，标准版和 Smart 使用 serial v1；共用 IRQ 时逐个检查已注册串口。
- `bsp/loongarch/common`：共用构建入口、工具链参数、内核 libc 映射和链接段布局；各 BSP 仅保留加载地址、次核栈大小和镜像名称。

UART 和 GMAC 由两个 BSP 的 `SConscript` 引入，目标文件分别放在各 BSP 的 `build/common` 下。`libcpu/loongarch/mach/common` 仅保留 LIOINTC 和 CPU 物理地址/非缓存地址转换定义。

DMA 使用链接脚本保留的 `.gmac_dma` 区域。它不属于 `.bss` 清零区或堆，也不由 ELF 加载段初始化；驱动只通过非缓存别名访问它。描述符和数据的物理地址必须处于 32 位 DMA 窗口。写入描述符字段和帧数据后，先执行屏障，再交出 OWN；接收时先取得所有权，再读取帧。

收发路径采用复制，避免把 lwIP pbuf 的生命周期交给 DMA。支持标准 MTU 和单 VLAN 帧，不启用巨帧、硬件校验和卸载、零复制或多队列。接收会检查首尾标志、错误状态和长度，去除 FCS；分配失败或坏帧也会回收描述符。中断只确认状态并通知 RX 工作线程，PHY 访问和 DMA 错误恢复在服务线程执行。

寄存器和描述符布局对照了参考 BSP 及 Linux 上游的 [DWMAC 描述符定义](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/stmicro/stmmac/descs.h)、[Loongson GMAC 支持](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/stmicro/stmmac/dwmac-loongson.c)、[LIOINTC 驱动](https://github.com/torvalds/linux/blob/master/drivers/irqchip/irq-loongson-liointc.c)。实现没有引入这些项目的驱动代码。

## 自动验证

在仓库根目录执行：

```sh
python3 examples/test/ls2k_gmac_test.py --build --output /tmp/ls2k-gmac-test
```

需要宿主 C 编译器及 AddressSanitizer/UndefinedBehaviorSanitizer、Python、SCons、kconfiglib 和 LoongArch musl 工具链。测试包含：

- MDIO/复位超时、无 PHY、地址越界、10/100/1000 Mbps 和双工切换。
- TX 环满、完成回收、发布 OWN 的顺序、RX 坏帧/非法长度/FCS 和环回绕。
- 两个独立网口模型各 100,000 帧、变长数据校验、复位后重建环。
- lwIP 接收通知在邮箱满后的重试和通知合并。
- GMAC0、GMAC1、双口 4 描述符/2 字节填充/直接 TX、双口 128 描述符/lwIP 2.2.1/IPv6、无网络标准版以及关闭 SMP 的标准版，共六种交叉构建。

脚本会恢复原 `.config` 并重新生成 `rtconfig.h`，日志和可执行文件放在指定目录；测试期间不要在同一 BSP 手动构建。宿主模型覆盖驱动状态和边界，不模拟总线时序或物理网络。

## 实板测试入口

在 menuconfig 中启用 `BSP_GMAC_USING_ECHO_TEST`，重新生成配置并构建。接入网线后，通过 `ifconfig` 查看 DHCP 地址，用 `gmac_status` 查看 PHY、链路、速率、收发和错误计数。

在板上运行：

```text
gmac_echo 120
```

该命令在 UDP 9000 端口运行 120 秒，然后关闭 socket 并打印统计。测试主机执行：

```sh
python3 examples/test/ls2k_gmac_test.py --peer 192.168.1.20 --frames 10000 --window 32
```

替换为板卡实际地址。主机逐帧校验序号和内容，并报告丢包、重复和耗时。测试两个网口时建议使用独立子网，分别执行；观察 `gmac_status` 确认流量经过预期端口。还应验证拔插网线、10/100/1000 Mbps 协商、连续压力下的错误计数及内存用量。

2026-10-14，使用 GCC 15.2.0 完成上述六种配置的交叉构建，无编译告警；宿主测试在 AddressSanitizer/UndefinedBehaviorSanitizer 下通过，两端口模型各完成 100,000 帧校验。UDP 校验客户端另通过本机回显 10,000 帧测试，该结果不代表板卡收发性能。

上述 2026-10-14 结果仅为模型和构建验证；2026-10-26 的实板收发结果见下文。AHCI 可选驱动沿用上一阶段迁入版本，不属于此次网络重构范围。

## 星云板首次实测（2026-10-26）

广东龙芯星云板实机由串口和 U-Boot 确认为 LS2K1000-DP / LA264，
PRID `0014a000`，双核、1 GiB DDR3。固件为 U-Boot 2022.04
`v2.0.0-00075-g29d49740-dirty`，报告 CPU 1000 MHz、总线 125 MHz。
串口为 UART0，115200 8N1。高内存窗口为
`0x9000000090000000..0x90000000bfffffff`；本 BSP 的加载地址和
64 MiB 堆位于该窗口内，不占用低内存的固件及显示缓冲区。

测试仅经 XMODEM 分段加载 RAM，每段及完整镜像均核对 CRC32；没有
烧写存储或执行 `saveenv`。独立 UART hello 返回 U-Boot 成功，单核和
双核标准版均启动。双核固件邮箱交接实测显示 `mask 0x3`。

已完成的实板测试包括：

- `port_test 5000`：调度、IPC、软定时器、堆和浮点上下文通过，堆占用
  测试前后均为 10256 字节。
- `smp_test 100000`：两核各 100000 轮，锁与原子累计 200000，迁移和
  IPI 检查通过，错误数为 0。
- `smp_reap_test 1000`：共 2000 个线程退出或远程删除并回收，通过。
- `ale_test`：28 种指令、100 轮、跨页及拒绝范围检查通过，实际恢复
  40000 次异常。首次镜像因缺少 BSP RAM 策略明确拒绝执行，补充
  `drivers/ls2k_unaligned.c` 后通过；只允许链接镜像和堆的缓存映射，
  排除设备、DMA、非缓存别名和对代码/异常向量的写入。

首次双核启动发现并发打印覆盖共享格式化缓冲区，标准和网络配置已启用
内核现有 `RT_USING_THREADSAFE_PRINTF`。该选项会增加同步打印时的
中断/调度延迟，时间敏感路径不应大量打印；没有修改公共打印实现。

该配置变更后的镜像经软件重启复测时出现 `SMP: CPU 1 did not start`，
停在启动断言。随后同一修正版镜像冷启动及软件重启后重新加载均通过
上述测试；该次失败原因仍未确认，不能据此宣称重复启动问题已经修复。

项目根目录的 `system/driver/tests/rtos/build_xingyun_kernel.py` 和
`test_xingyun_serial.py` 保留隔离构建及串口复测流程。以上条目是标准版
内核基础验证，不能代替长时间稳定性测试。Smart UI 的验证范围见下文。

## 星云板 Smart 与 NovaUIOS

`smart_defconfig` 启用双核、软件页表、ROMFS/TMPFS。内核从物理
`0x98000000` 加载，堆结束于 `0xa0000000`，页分配器使用
`0xa0000000..0xb8000000`。NovaUIOS 运行在独立 Smart 用户进程中。

`BSP_USING_XINGYUN_LCD` 校验并接管固件 DVO1 的 1024×600、ARGB8888
显示：保留像素时钟、引脚复用及物理 `0x0dc00000` 的固件显存，使用两块
私有绘制缓冲区，在提交时复制到扫描缓冲区。该方式不保证垂直同步，
不能视为硬件双缓冲翻页。2026-10-26 双核 Smart 实板启动达到
`NOVAUI READY`，进程 `active=1`、显示错误数为零；用户确认同一块
ATK-MD0700R-1024x600 屏上的文字、布局和显示清晰正常。

`BSP_USING_XINGYUN_TOUCH` 接入物理 I2C1（Linux DTS 的 i2c3，
`0x1fe21800`），使用 125 MHz 总线时钟、100 kHz I²C、字节寄存器访问。
只探测 Goodix 的 `0x5d` 和 `0x14` 地址，复用项目 GT9xx 单点解析器。
采用线程上下文轮询，保留触摸芯片出厂范围、校验和及中断模式；不猜测
复位 GPIO、不改写完整配置。`RT_TOUCH_PIN_IRQ` 应关闭。
`xingyun_touch_status` 查看总线传输及失败计数，不消耗 UI 的触摸报告。
2026-10-26 实板识别 GT911（地址 `0x5d`），范围 1024×600；用户确认
点击和拖动正常、位置准确，观测期间总线失败数和 UI 显示错误数均为零。
本次验证为单点触摸，不代表多点手势或长期稳定性已通过。

项目 `system/driver/tests/rtos/build_xingyun_kernel.py --application ELF`
构建隔离的双核 Smart UI 镜像，`test_xingyun_novaui.py` 加载 gzip uImage、
校验压缩和解压镜像、检查两核上线及 UI 状态。测试只写 RAM。
当前开发和后续网口、USB 摄像头测试以星云板为主，每次启动双核并核对
`SMP: 2 CPUs online (mask 0x3)`；已连接网线及摄像头不等于驱动已通过验证。

## LS2K0300

代码位于 `bsp/loongarch/ls2k0300`。芯片参数依据龙芯官网的
[《龙芯 2K0300 处理器用户手册》V1.1（2026-08）](https://www.loongson.cn/uploads/images/2026081120140475319.%E9%BE%99%E8%8A%AF2K0300%E5%A4%84%E7%90%86%E5%99%A8%E7%94%A8%E6%88%B7%E6%89%8B%E5%86%8C_V1.1.pdf)，
主要核对第 2 章、3.2、3.4、3.5、5 和 14 章。手册与下载文件不存入代码仓库。

| 项目 | LS2K1000LA | LS2K0300 |
| --- | --- | --- |
| CPU 移植 | 共用 LoongArch64 / LP64D，SMP 默认 2 核 | 共用 LoongArch64 / LP64D，SMP 默认 1 核 |
| UART0 / 步长 | `0x1fe20000` / `0x100` | `0x16100000` / `0x400` |
| 传统中断控制器 | `0x1fe01400`，路由值 `0x11` | `0x16001400`，路由值 `0x10`，低 4 位保留 |
| GMAC 资源 | 读取固件 PCI BAR | 固定 MMIO `0x16020000` / `0x16030000` |
| GMAC IRQ | 12 / 14 | 49 / 50（传统中断） |
| 内核加载地址 | `0x9000000098000000` | `0x9000000000200000` |

2K0300 使用传统中断，关闭固件可能留下的扩展中断通路；不访问 2K1000LA 专有的 BOUNCE/AUTO 寄存器。Nano、标准版、Smart 共用启动、上下文、定时器和 CPU 异常代码，没有复制另一套 CPU 移植。两款芯片与 QEMU 共用 IOCSR 邮箱/IPI 驱动；2K0300 的配置限定为 1 核。2K1000LA 次核需要固件停驻在邮箱等待入口，启动核向邮箱 0 写入次核入口物理地址并发送 IPI；5 秒内未上线会报错。固件交接协议仍需实板核验。

```sh
cd bsp/loongarch/ls2k0300
export RTT_EXEC_PATH="$HOME/local/loongarch64-linux-musl-gcc-15.2.0/bin"
cp config/smart_defconfig .config  # 其他配置同样位于 config/
scons --pyconfig-silent
scons -j8
```

默认 `.config` 为标准版；`network_defconfig` 启用双 GMAC，Smart 也可在 menuconfig 中启用同一网络驱动。`smart_defconfig` 使用临时根文件系统，重启后数据丢失；尚未提供 SDIO/eMMC 存储驱动或完整 musl 用户态发行环境。系统调用沿用仓库官方表，不重新编号。2K0300 和 2K1000LA 均只使用软件 TLB refill，Kconfig 不允许选择硬件 PTW；2K1000LA 的 Smart 板级内存初始化使用高 DDR，避免占用固件显示缓冲区。

`BSP_UART_INDEX` 选择控制台 UART0～9，设备名固定为 `uart`。默认 UART0，115200、8N1；实际调试口若接在其他 UART，需调整该选项。驱动读取采样模式、DDR PLL 和 APB 分频寄存器得到时钟；`BSP_UART_CLOCK_HZ` 非零时可明确覆盖输入频率。PLL、DDR 初始化、引脚复用、外部 PHY 复位及 RGMII 延迟由引导固件完成，驱动不会猜测板上连接或重写这些设置。

内核放在低 DDR 的 2 MiB 处，链接区域限制为 16 MiB，堆从 `_end` 到 32 MiB。Smart 页分配区为 32 MiB～`BSP_RAM_END`，默认上界 128 MiB；应按实际可用内存和固件保留区调整。上界最多 `0x0ff00000`，避开可能仍打开的 DDR 控制器寄存器窗口。GMAC DMA 存储仍在 `.gmac_dma` 中，只接受低 256 MiB DDR 的地址，并为对应端口选择非缓存 DDR 通路；不把 CPU 高 DDR 别名直接当成 DMA 地址。

两个 BSP 的 `BSP_USING_SELFTEST` 提供 `port_test`；启用 SMP 后另有 `smp_test` 和 `smp_reap_test`，Smart 还有 `port_resources`、`port_mmu` 和 `smp_tlb_test`。UART、传统中断控制器和 GMAC 的共享寄存器使用自旋锁保护，设备 IRQ 仍路由到启动核。网络压力测试继续使用上文同一套 `gmac_echo` 和主机脚本。自动检查命令：

```sh
python3 examples/test/ls2k_platform_test.py
python3 examples/test/ls2k_gmac_test.py --build --bsp ls2k0300 --output /tmp/ls2k0300-test
```

2K0300 构建矩阵覆盖五种基础/网络组合、关闭 SMP 的标准版，以及 Nano、Smart、UART9 和 Smart 双网口，共十种。寄存器模型检查时钟计算、中断路由/确认/屏蔽、串口 DLAB 互斥和 DMA 通路；模型不能验证电气时序。现有 QEMU `virt` 不模拟 2K0300 外设，实板启动、串口时钟、PHY 协商与持续收发仍需硬件验收。当前没有板级复位/关机实现，使用板上复位键或引导固件。

本次验证（2026-10-14）：2K1000LA 六种配置、2K0300 十种配置交叉构建通过，包含 SMP 和关闭 SMP 的标准版。共用驱动的宿主测试在 ASan/UBSan 下通过，两个 GMAC 模型各校验 100,000 帧；两种芯片的串口模型各执行 100,000 次轮询收发。QEMU 的多核内核、用户态和压力测试记录见 `bsp/qemu-virt64-loongarch/README.md`。这些结果不能代替两款芯片的固件交接、次核启动和网络实板验收。


## 星云板 Smart 网络与 USB 实测入口（2026-10-26）

项目根目录执行 `system/driver/tests/rtos/build_xingyun_kernel.py`，在原有
`--cpus 2 --application ELF` 参数上增加 `--network --camera`，可构建同时
包含 NovaUIOS、双 GMAC 和 UVC 的隔离镜像。保持双核；加载脚本在进入
RT-Thread 前执行 U-Boot `usb stop`，避免固件 USB DMA 继续访问内存。

网络修正均在 LoongArch BSP 和板级配置中：

- 系统工作队列栈由 2048 增至 8192 字节。原镜像实测溢出，修正后网络
  压力期间峰值约 26%，未再出现该溢出。
- 对已识别的 YT8511（PHY ID `0000:010a`），清除扩展寄存器 `0x27`
  的自动休眠位 15，并有界等待休眠状态位 5 清零；保留 RGMII 延迟和
  时钟输出设置，恢复 MDIO 扩展寄存器选择。仅设置 PLL 保持位的版本
  在未接线的 e1 上仍然超时，不能作为修复。关闭自动休眠后两口初始化
  均成功。位定义来自 [YT8511C/H 数据手册](https://atta.szlcsc.com/upload/public/pdf/source/20240105/D2398836F2709CC9BC3B5FCE6170D0BE.pdf)。
- 移除初始 `ETHIF_LINK_PHYUP` 标志，由 PHY 轮询发布真实链路变化；该
  标志原先错误地把未接线端口报告为 LINK_UP。
- BSP `lwipopts.h` 包含公共配置后覆盖 UDP 邮箱 64、接收字节预算
  128 KiB、NETBUF/INPKT 池各 128。仅增大邮箱而保留 8192 字节预算的
  版本仍丢包。网络测试配置同时使用 128 描述符的 DMA 环。

第一轮网络镜像实测：e0 协商 1000 Mbps 全双工并通过 DHCP 获取地址；
主机发起 1472 字节 ping 及板端普通 ping 均零丢包。窗口 16 的 10000
包 UDP 全部回显且内容一致，耗时 1.567 秒；TCP 双向各 64 MiB 逐字节
校验通过，耗时 9.328 秒。两项测试前后网卡错误计数没有增加，静止界面
下堆占用均为 27803856 字节。窗口 32 的进一步压力在 15104 包内丢失
一个回显；该失败已保留，不能用首轮通过结果代替更高压力验证。

保留的实板测试脚本位于项目 `system/driver/tests/rtos/`：

- `test_xingyun_network.py`：双向 ping、变长 UDP 序号与内容校验、网卡
  计数、线程栈、堆及 UI 状态；同时记录宿主 UDP 统计。
- `test_xingyun_tcp.py`：固定 64 KiB 块、部分收发处理、双向大容量流校验。
- `test_xingyun_camera.py`：枚举、模式、完整帧、连续采集与重复启停检查。

USB 使用星云板 PCI `00:04.1` 固件 BAR 和 IRQ 50，复用 2K0300 BSP
已有的 EHCI/UVC 传输实现，不修改 RT-Thread 公共 USB 栈。控制器有
三个硬件根端口，当前适配明确只暴露端口 1；其余根端口、OHCI、OTG
尚未实现。摄像头实物为 Sonix `0c45:636b`，UVC 1.0，高速 480 Mbps。

实板发现 U-Boot `usb stop` 将伴随 OHCI 留在 Suspend（HcControl
`0xc0`），此时 EHCI 即使运行且端口供电也检测不到连接。仅修改端口
供电无效；板级初始化检查 PCI `00:04.2` 后屏蔽 OHCI 中断、关闭所有
DMA 列表并置 USB Reset，随后 EHCI 可检测并枚举摄像头。该操作用于
控制器交接，不代表实现了 OHCI 的低速/全速传输。

项目 UVC 驱动对 1.0 设备使用固定 26 字节 Probe/Commit 控制块，避免
依赖此摄像头不兼容的 GET_LEN 响应；仍检查 GET_CUR 的完整长度、
格式、间隔和缓冲区边界。双核无界面诊断镜像实测 YUYV 640×480
三轮、每轮 10 秒共取得 855 帧，约 28.5 帧/秒；每轮丢弃 1 帧，负载
格式、传输和溢出错误均为零，测试前后堆占用同为 118992 字节。
这些结果尚不代表与 NovaUIOS 同时运行的验证。

MJPEG 首轮 800×600 有大量丢弃，诊断发现有效 JPEG 的 `FFD9` 后带
零填充；原先要求最后两个传输字节为 `FFD9`，会误拒绝这些帧。
已经保留修正及回归用例，拒绝非零尾部、缺失 EOI 和带传输错误的帧。
修正后的 MJPEG 800×600 实板三轮 10 秒取得 855 帧，随后四轮 30 秒
取得 3540 帧（平均 29.5 帧/秒）；每轮丢弃 1 帧，负载格式、传输和
溢出错误均为零，堆占用前后保持 118992 字节。
另经串口导出一张 39088 字节实拍 JPEG，长度和 FNV 校验一致，宿主
完整解码得到 800×600 图像。导出脚本为
`system/driver/tests/rtos/export_xingyun_camera_frame.py`；实拍文件留在
本机临时目录，测试记录中不包含照片内容。

网络进一步诊断曾复现回显尾部 7 字节变化、UDP 校验错误，以及接收
缺包和应用内容不一致；发送前校验与 DMA 回收哈希计数为零也不能
排除接收路径问题。随后发现本板 CFG0（物理地址 `0x1fe00420`）的
`gmac_coherence_en` 位 0 仍为 1，与驱动使用的 SUC 非缓存缓冲区
访问方式不一致。板级初始化在 MAC 启动前仅清除此位，保留其他位，
加入屏障和读回检查；实板值由 `00003d09` 变为 `00003d08`。

修正后双核 NovaUIOS 镜像在主页运行，UDP 窗口 32、100000 包全部
逐字节回显校验通过，耗时 73.933 秒；无丢包、重复或内容损坏，RX/TX
校验、内容、回收及 pbuf 丢弃计数均为零。诊断代码有额外开销，此结果
不代表关闭诊断后的性能或长期稳定性；堆占用由 27823568 增至
27824336 字节，不能声称完全不变。板级寄存器宿主模型覆盖保留位、
重复初始化、两个端口及失败路径，但不能模拟真实总线时序。
相关成功和失败记录均保存在项目
`system/docs/rthread/bugs/tests/results/20261026-xingyun-physical/`。


### 星云板持久安装与自动启动

2026-10-26 已按用户要求用双核 RT-Thread Smart + NovaUIOS 镜像替换
SATA `scsi 0:1 /boot/uImage`，没有保留原 Starry 系统镜像副本。
U-Boot 本身、分区表及板级数据保留。安装镜像大小 3529572 字节，
CRC32 `818587cb`；写入后重新读取，长度、CRC32 和 uImage 内部校验
全部通过。内核以 gzip 打包，加载和入口物理地址为 `0x98000000`。
uImage 使用 vendor `bootm start` / `bootm loados` 解压路径，其头部
Linux 类型仅用于兼容固件镜像处理，实际负载是 RT-Thread Smart。

这版 U-Boot 使用 simple parser，不支持 `if/then` 或 `&&`。最初使用
条件语法导致复位停留在 U-Boot；已改为以下环境变量，并验证保存后
自动执行。`run` 依次执行变量，某一步返回错误即停止后续启动步骤。

```text
bootcmd=run novaui_boot
novaui_console=setenv stdin serial; setenv stdout serial; setenv stderr serial
novaui_scan=scsi reset
novaui_load=ext4load scsi 0:1 9000000090000000 /boot/uImage
novaui_check=iminfo 9000000090000000
novaui_prepare=bootm start 9000000090000000
novaui_unpack=bootm loados
novaui_stop=usb stop
novaui_go=go 9000000098000000
novaui_boot=run novaui_console novaui_scan novaui_load novaui_check novaui_prepare novaui_unpack novaui_stop novaui_go
```

持久化 stdout/stderr 保留 `serial,vidconsole,vidconsole1`，供固件初始化
显示；启动链随后重定向到串口并停止固件 USB，再进入内核。长命令通过
串口分段发送，避免显示回显较慢时 UART FIFO 丢字节；保存前逐项读回
比对。安装脚本 `system/driver/tests/rtos/install_xingyun_boot.py` 默认
只检查，指定 `--write` 才覆盖选定镜像并保存环境；它要求已有双核 RAM
启动通过记录、原环境备份、目标文件的长度和 CRC，避免误选目标。

`system/driver/tests/rtos/test_xingyun_autoboot.py` 实测从 U-Boot 执行
复位，全程不重新上传镜像、不截断自动启动，约 79.51 秒出现
`NOVAUI READY`。日志确认从 SATA 读取镜像、两核上线、界面 active=1
且 errors=0；随后打开智能相机，5 秒检查时累计收到 127 帧，启动阶段
丢弃 1 帧。此结果验证了复位自动启动，尚未验证安装后的完全断电启动。
安装校验、旧脚本失败和修复后的完整记录位于项目测试结果目录的
`20261026-xingyun-physical/installation/`。
