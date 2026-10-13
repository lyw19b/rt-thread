# LS2K1000LA 与 LS2K0300

本 BSP 面向 LoongArch 版龙芯 2K1000LA，使用单核 RT-Thread 标准版。板级地址和外设连接参考 `rt-thread-2k1000/bsp/loongarch/ls2kdev`，网络驱动已重新组织实现。Nano、Smart 的 QEMU 配置仍在 `bsp/qemu-virt64-loongarch`；本板尚未实现 Smart 地址空间初始化。

## 构建

```sh
cd bsp/loongarch/ls2kdev
export RTT_EXEC_PATH="$HOME/local/loongarch64-linux-musl-gcc-15.2.0/bin"
cp network_defconfig .config  # 双网口、lwIP 2.1.2、DHCP、ping、SAL sockets
scons --pyconfig-silent
scons -j8
```

`standard_defconfig` 保留不启用网络的基础配置。两种配置均不依赖外部软件包。用 `scons --menuconfig` 调整配置，再由 SCons 生成 `rtconfig.h`。

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

- `libcpu/loongarch/ls2k/gmac/gmac.h`：寄存器和描述符定义、核心接口。
- `libcpu/loongarch/ls2k/gmac/gmac.c`：MDIO、PHY、MAC 初始化、描述符所有权和环索引，不依赖 lwIP。
- `libcpu/loongarch/ls2k/gmac/drv_gmac.c`：RT-Thread 设备注册、pbuf 转换、同步、中断通知、链路检查和总线错误恢复。
- `drivers/drv_gmac_platform.c`：板级 GMAC 资源获取与 DMA 地址范围。
- `drivers/drv_pci.c`：2K1000LA 的 BAR 检查和 PCI 命令。
- `libcpu/loongarch/ls2k/drv_interrupt.c`：两款芯片共用的传统 LIOINTC 驱动，包含边沿确认、屏蔽和分发。
- `libcpu/loongarch/ls2k/drv_uart.c`：共用串口驱动，Nano 使用轮询，标准版和 Smart 使用 serial v1；共用 IRQ 时逐个检查已注册串口。
- `bsp/loongarch/common`：共用构建入口、工具链参数、内核 libc 映射和链接段布局；各 BSP 仅保留加载地址、次核栈大小和镜像名称。

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
- GMAC0、GMAC1、双口 4 描述符/2 字节填充/直接 TX、双口 128 描述符/lwIP 2.2.1/IPv6、无网络标准版共五种交叉构建。

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

2026-10-14，使用 GCC 15.2.0 完成上述五种配置的交叉构建，无编译告警；宿主测试在 AddressSanitizer/UndefinedBehaviorSanitizer 下通过，两端口模型各完成 100,000 帧校验。UDP 校验客户端另通过本机回显 10,000 帧测试，该结果不代表板卡收发性能。

当前尚未进行实板收发验证。现有主线 LoongArch QEMU 只有 `virt` 机型，不能代替 LS2K1000LA GMAC 验证。AHCI 可选驱动沿用上一阶段迁入版本，不属于此次网络重构范围。

## LS2K0300

代码位于 `bsp/loongarch/ls2k0300`。芯片参数依据龙芯官网的
[《龙芯 2K0300 处理器用户手册》V1.1（2026-08）](https://www.loongson.cn/uploads/images/2026081120140475319.%E9%BE%99%E8%8A%AF2K0300%E5%A4%84%E7%90%86%E5%99%A8%E7%94%A8%E6%88%B7%E6%89%8B%E5%86%8C_V1.1.pdf)，
主要核对第 2 章、3.2、3.4、3.5、5 和 14 章。手册与下载文件不存入代码仓库。

| 项目 | LS2K1000LA | LS2K0300 |
| --- | --- | --- |
| CPU 移植 | 共用 LoongArch64 / LP64D，单启动核 | 共用 LoongArch64 / LP64D，单核 |
| UART0 / 步长 | `0x1fe20000` / `0x100` | `0x16100000` / `0x400` |
| 传统中断控制器 | `0x1fe01400`，路由值 `0x11` | `0x16001400`，路由值 `0x10`，低 4 位保留 |
| GMAC 资源 | 读取固件 PCI BAR | 固定 MMIO `0x16020000` / `0x16030000` |
| GMAC IRQ | 12 / 14 | 49 / 50（传统中断） |
| 内核加载地址 | `0x9000000098000000` | `0x9000000000200000` |

2K0300 使用传统中断，关闭固件可能留下的扩展中断通路；不访问 2K1000LA 专有的 BOUNCE/AUTO 寄存器。Nano、标准版、Smart 共用启动、上下文、定时器和 CPU 异常代码，没有复制另一套 CPU 移植。

```sh
cd bsp/loongarch/ls2k0300
export RTT_EXEC_PATH="$HOME/local/loongarch64-linux-musl-gcc-15.2.0/bin"
cp smart_defconfig .config  # 也可选 nano、standard、network_defconfig
scons --pyconfig-silent
scons -j8
```

默认 `.config` 为标准版；`network_defconfig` 启用双 GMAC，Smart 也可在 menuconfig 中启用同一网络驱动。`smart_defconfig` 使用临时根文件系统，重启后数据丢失；尚未提供 SDIO/eMMC 存储驱动或完整 musl 用户态发行环境。系统调用沿用仓库官方表，不重新编号。

`BSP_UART_INDEX` 选择控制台 UART0～9，设备名固定为 `uart`。默认 UART0，115200、8N1；实际调试口若接在其他 UART，需调整该选项。驱动读取采样模式、DDR PLL 和 APB 分频寄存器得到时钟；`BSP_UART_CLOCK_HZ` 非零时可明确覆盖输入频率。PLL、DDR 初始化、引脚复用、外部 PHY 复位及 RGMII 延迟由引导固件完成，驱动不会猜测板上连接或重写这些设置。

内核放在低 DDR 的 2 MiB 处，链接区域限制为 16 MiB，堆从 `_end` 到 32 MiB。Smart 页分配区为 32 MiB～`BSP_RAM_END`，默认上界 128 MiB；应按实际可用内存和固件保留区调整。上界最多 `0x0ff00000`，避开可能仍打开的 DDR 控制器寄存器窗口。GMAC DMA 存储仍在 `.gmac_dma` 中，只接受低 256 MiB DDR 的地址，并为对应端口选择非缓存 DDR 通路；不把 CPU 高 DDR 别名直接当成 DMA 地址。

`BSP_USING_SELFTEST` 提供 `port_test`、`port_resources` 等回归入口；网络压力测试继续使用上文同一套 `gmac_echo` 和主机脚本。自动检查命令：

```sh
python3 examples/test/ls2k_platform_test.py
python3 examples/test/ls2k_gmac_test.py --build --bsp ls2k0300 --output /tmp/ls2k0300-test
```

2K0300 构建矩阵覆盖五种基础/网络组合以及 Nano、Smart、UART9 和 Smart 双网口。寄存器模型检查时钟计算、中断路由/确认/屏蔽、串口 DLAB 互斥和 DMA 通路；模型不能验证电气时序。现有 QEMU `virt` 不模拟 2K0300 外设，实板启动、串口时钟、PHY 协商与持续收发仍需硬件验收。当前没有板级复位/关机实现，使用板上复位键或引导固件。

本次验证（2026-10-14）：2K1000LA 五种配置、2K0300 九种配置交叉构建通过。共用驱动的宿主测试在 ASan/UBSan 下通过，两个 GMAC 模型各校验 100,000 帧；两种芯片的串口模型各执行 100,000 次轮询收发。QEMU `virt` 重新运行 Nano/标准版/Smart 官方测试，分别通过 19/19/32 项，每种配置通过 180 秒定时器压力及三轮双线程各 20,000 次迭代。Smart 另通过三轮各 1,024 次 fork/CoW，堆与页计数保持 92,896 字节、31,603/31,690。QEMU 结果验证共用内核，不代表 2K0300 已通过实板运行验收。
