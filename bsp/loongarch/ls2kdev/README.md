# LS2K1000LA

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

- `drivers/net/gmac.h`：寄存器和描述符定义、核心接口。
- `drivers/net/gmac.c`：MDIO、PHY、MAC 初始化、描述符所有权和环索引，不依赖 lwIP。
- `drivers/net/drv_gmac.c`：RT-Thread 设备注册、pbuf 转换、同步、中断通知、链路检查和总线错误恢复。
- `drivers/drv_pci.c` / `drv_interrupt.c`：BAR 检查、PCI 命令和 LIOINTC 路由、屏蔽。

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
