# RT-Thread LoongArch 项目交接

更新时间：2026-10-22。本次仅更新交接文档，核对提交、工具路径和已有测试记录，未重新运行构建或回归；最近一次实现验证仍为 2026-10-20。本文面向下一次在本仓库工作的 Codex 和维护者，记录项目状态、代码入口、构建测试方法及尚未完成的验证。后续修改代码时，应同步更新相关状态，不能把历史测试结果当作新提交的验证结果。

## 1. 下次启动先看这里

- 当前仓库：`/home/airxs/user/aicode/kernel/rt-thread`。
- 当前分支：`rt-loong64-v0.1`。最近实现提交为 `8c3bce55b0`（页故障修复上下文检查）；此前 `dbcb780b13` 修复 MMU 回滚及 IRQ 状态，`17928ab622` 重构异常入口和汇编别名。更新本文前工作区干净；启动新任务时仍应重新检查提交和工作区状态。
- 参考仓库：`/home/airxs/user/aicode/kernel/rt-thread-2k1000`。仅用于核对硬件行为和移植差异，不直接照搬其驱动结构或代码风格。
- 本地架构参考：`/home/airxs/user/aicode/system/linux/linux-6.11-labcore`。可只读核对 LoongArch 指令、ABI 和硬件语义；本项目实现及注释应围绕自身行为组织，不搬用 Linux 实现或注释，保留必要的版权与许可声明。
- 当前内核版本宏为 RT-Thread 5.3.1；不能据此推断本分支与上游最新版本完全一致。
- 先读 [AGENTS.md](../AGENTS.md)、本文及具体 BSP 的 README，再读取任务涉及的源码。
- 本文按用户明确要求放在 `docs/PROJECT_OVERVIEW.md`。其他文档仍遵守仓库放置规则，不随意新建 Markdown。

```sh
cd /home/airxs/user/aicode/kernel/rt-thread
git status --short
git branch --show-current
git log -12 --oneline
git diff --stat
```

已有持续要求：代码清晰、可维护，公共代码只保留一份；遵循 RT-Thread 风格并有必要的注释；阶段性提交；Nano 和 Smart 都要回归及压力测试，Smart 要覆盖用户态、MMU 和多核。实板由用户后续提供，不能宣称已经完成实板验收。

**当前修改边界：用户已再次明确“仍只允许修改 LoongArch，保留公共代码不变”。** 继续修复时只在授权的 LoongArch 实现及直接相关测试、文档内工作；`components/`、`src/`、公共 `include/` 保持不变，不能因 `components/lwp/arch/loongarch/` 含架构名就自行扩大范围。只由一个 Agent 工作，不启动子代理。`regdef.h`、`fpregdef.h` 保留；汇编别名保持 `ADD`、`ADDI`、`STUB`、`LD`、`ST`、`FLD`、`FST`，不恢复 `LONG_` 前缀，也不据此宣称已支持 32 位。

**当前结论：架构层的次生缺页断言已修复，多核进程压力的根因尚未全部修复。** 最新八组 UP/四核回归通过，四核软件 refill、硬件 PTW 各追加三轮 `stress 4096`、六轮 `parallel 4 1024` 通过且资源恢复基线；但此前软件路径曾到第八轮才失败，公共 LWP/procfs 缺陷仍保留，不能标记为多核长期稳定。

下次定位优先读取以下证据，详细过程及归因边界见第 8 节。路径位于仓库外，2026-10-22 核对了最新验证汇总及源码校验值；临时目录以后可能被清理。

| 事项 | 当前状态与记录入口 |
| --- | --- |
| 最新架构修复及回归 | `8c3bce55b0`；`/tmp/la-fault-context-fix/summary.json` 为八组回归，`injection-v2-summary.json` 为四组新旧受控故障对照，`pressure/summary.json` 为追加压力 |
| LWP 重复释放 PID 引用 | 已捕获重复递减最后引用及活动线程使用已释放 LWP 的现场，未修复；`/tmp/la-null-fault/no-procfs-diagnostic/pid0-break-gdb.log`、`assert-details-gdb.log` |
| procfs 遍历与摘除竞争 | 已捕获摘除节点后遍历继续读取该节点的实际访存链，未修复；`/tmp/la-cause-analysis/hardware-chain.log`、`hardware-trace-gdb.log` |
| 自然压力失败 | 软件 refill 和硬件 PTW 均有失败，不能互相排除；`/tmp/la-pressure-20261020/summary.json` 及各模式故障快照 |

继续分析时区分首次异常与后续断言、其他 CPU 等待 TLB 确认的现场。不能在架构切换或异常处理中跳过损坏对象，也不能以关闭 SMP/procfs、缩减压力或删除断言替代根因修复；临时隔离实验的结果须单独记录。

## 2. 当前支持范围与未完成事项

| 目标 | 配置与默认核数 | Smart / 页表遍历 | 验证边界 |
| --- | --- | --- | --- |
| QEMU LoongArch `virt` | Nano、标准版、Smart；默认 SMP 4 核，可关闭 SMP，配置范围 1～16 核 | 软件 refill、硬件 PTW | 最新八组 UP/四核回归通过；两种四核 Smart 路径的历史进程压力故障及 16 核停滞仍未完成稳定性验收 |
| LS2K1000LA | 标准版、网络版；默认 SMP 2 核，可改 1 核或关闭 SMP | **板级 Smart 内存初始化未移植**；芯片只允许软件 refill | 交叉编译、宿主驱动模型通过；未实板启动或联网验收 |
| LS2K0300 | Nano、标准版、网络版、Smart；默认开启 SMP 框架，限定 1 核 | 软件 refill；Smart 使用临时根文件系统 | 交叉编译、宿主驱动模型通过；未实板验收 |

本项目的 `ls2k1000` 指 LoongArch 版 **2K1000LA**，不要与仓库内其他 MIPS 龙芯 BSP 混淆。2K1000 当前没有完整 Nano/Smart defconfig 集合；不要把 QEMU 或 2K0300 的能力写成该板已经具备。

后续需要继续完成：

1. **16 核 Smart 软件路径组合压力停滞。** 历史上发生过输出停滞，尚未完成长时间稳定性验收。`c5a8816f3e` 修复了 TLB 测试自身的启动屏障问题，但不能据此判定所有停滞都已解决。应重新保存完整复现日志、ELF、配置及多核栈，再判断是否还有内核问题。
2. **LS2K1000LA Smart 板级内存初始化。** `bsp/loongarch/common/build.py` 当前显式拒绝此组合。需要完成内存布局、保留区和地址空间初始化，不能仅删除报错来“支持”Smart。
3. **完整 Smart 用户态环境。** 当前独立 ABI 测试程序不链接 Linux 用户态 libc；尚不能代替 Smart musl、动态链接器及真实应用的验证。
4. **实板测试。** 两款芯片的固件交接、串口时钟、次核启动、PHY、RGMII 延迟、DMA、双网口及长压均需真实硬件。QEMU `virt` 不模拟这些板载外设。
5. 2K0300 尚无 SDIO/eMMC 存储驱动及板级复位/关机实现。2K1000 的可选 AHCI 沿用此前迁入版本，不属于已完成的网络驱动重构范围。
6. **Smart 四核并行进程压力失败。** 后续已分别捕获 procfs 遍历使用已摘除节点、LWP 重复释放 PID 引用的故障链，软件 refill 和硬件 PTW 均有失败记录。用户明确保持公共代码不变，这两项公共组件缺陷尚未修复。架构层的 MMU、IRQ 和页故障报告改动不能替代其生命周期修复，详见第 8 节。

不在当前验证范围内：LSX/LASX、CPU 热插拔、主线 QEMU VirtIO PCI 网络/磁盘、完整 Smart 用户态发行环境。

## 3. 目录与代码入口

| 路径 | 职责 |
| --- | --- |
| `src/`、`include/` | RT-Thread 内核及公共接口；调度、IPC、线程生命周期等 |
| `libcpu/loongarch/common/` | 共用启动、上下文、异常、MMU/TLB、SMP/IPI、定时器、栈和缓存接口 |
| `libcpu/loongarch/common/include/` | 寄存器、异常帧、页表、汇编偏移及 CPU 接口 |
| `libcpu/loongarch/ls2k/` | LS2K 传统 LIOINTC 中断及 CPU 地址转换定义；不放 UART/GMAC |
| `components/lwp/arch/loongarch/loong64/` | Smart 架构适配，用户上下文、信号、fork/clone 等 |
| `components/lwp/`、`components/mm/` | 通用 Smart 进程、系统调用、信号和地址空间实现 |
| `components/drivers/`、`components/net/` | 通用设备框架、网络栈和 lwIP 适配 |
| `bsp/qemu-virt64-loongarch/` | QEMU BSP、三个 defconfig、运行脚本、RAM 磁盘和平台驱动 |
| `bsp/loongarch/common/` | 两款 LS2K 共用的 UART、GMAC、构建配置、链接段布局 |
| `bsp/loongarch/ls2k1000/` | 2K1000LA 板级地址、PCI、GMAC 资源、固件接口和配置 |
| `bsp/loongarch/ls2k0300/` | 2K0300 板级地址、时钟、固定 MMIO 资源和配置 |
| `examples/test/` | LoongArch 内核/用户/SMP 测试、QEMU 驱动脚本、LS2K 宿主模型及 UDP 校验工具 |
| `tools/` | SCons/Kconfig 等仓库构建支持 |
| `documentation/7.contribution/` | 仓库编码规范 |

重要命名已调整，不要恢复旧名称：

- `bsp/loongarch/ls2kdev` 已改为 `bsp/loongarch/ls2k1000`。参考仓库路径中的 `ls2kdev` 仍可能有效；其他架构的同名 BSP 与此无关。
- LS2K 中断实现为 `libcpu/loongarch/ls2k/platform_interrupt.c`，各板接口头为 `drivers/platform_interrupt.h`；`liointc.h` 是共用寄存器定义。
- 共用 UART 是 `bsp/loongarch/common/drv_uart.c`，GMAC 是该目录下的 `gmac/`，不再放在 `libcpu`。
- 每个 BSP 的 `SConscript` 通过独立 `variant_dir='common'` 引入共享驱动，构建输出分别进入各 BSP 的 `build/common`。不要让不同 BSP 共享一份带配置宏的目标文件。

### 核心调用关系

启动从 `startup_gcc.S:_start` 进入，经 `setup.c:init_cpu_early()` 初始化 CPU 异常及 Smart 页表遍历配置，然后进入 `rtthread_startup()`；启动流程调用板级 `rt_hw_board_init()` 初始化内存、串口和定时器等，随后启动调度。次核从 `loongarch_secondary_entry` 进入 `smp.c:loongarch_secondary_start()`。

- 线程栈与上下文：`stack.c`、`context_gcc.S`、`cpuport.c`、`cpuport_gcc.S`、`include/stackframe.h`。修改寄存器保存布局时同时核对汇编偏移、用户上下文和信号恢复。
- 汇编指令别名：`include/asm-generic.h` 提供 `ADD`、`ADDI`、`STUB`、`LD`、`ST`、`FLD`、`FST`；当前仍只映射 64 位指令。去掉 `LONG_` 前缀仅是命名调整，32 位栈布局、启动、MMU 和 ABI 尚未实现。
- 寄存器定义：`include/loongarch_regs.h` 保存 C/汇编共用的硬件编码，`include/loongarch.h` 提供 C 访问接口。硬件数值、原有宏名和栈偏移保持兼容；`regdef.h`、`fpregdef.h` 按用户要求保留。
- 非对齐模拟：`unaligned.c` 按表解码 28 种标量访存指令，`include/inst.h` 仅声明访问描述及模拟接口，`unaligned_gcc.S` 按字节读写。当前 `do_ale()` 仍执行致命异常策略，未接入模拟器；模拟器直接访问 live FPR，不提供访存异常修复，不能直接改接到现有异常返回路径。
- 异常与页故障：`exception_gcc.S`、`trap.c`；页表操作在 `mmu.c`，页表遍历初始化在 `setup.c`。当前为 LP64D、4 KiB 页、三级页表，禁用 LSX/LASX。
- 同步异常共用 `loongarch_exception_entry` 保存上下文，由 `loongarch_dispatch_exception()` 按保存的 `ESTAT` 分发；原 `handle_ade` 等向量符号是该入口的别名。中断仍走延迟调度和 Smart 用户返回处理，页故障修复后直接恢复并重试。向量 stub 长度 40 字节，处理函数指针位于偏移 32。
- SMP：`smp.c` 共用 IOCSR 邮箱和 IPI；邮箱 0 传次核入口物理地址。每核独立异常入口、时钟、栈，设备 IRQ 仍交启动核。2K1000 次核固件需处于约定的邮箱等待状态，5 秒未上线报错。
- TLB：页表修改同步等待在线核确认失效；关中断等待自旋锁时调用 `loongarch_tlb_poll()`，避免页表锁与 IPI 相互等待。不能在确认远端失效之前回收仍可能被使用的页表。
- 缓存接口：当前 `cache.c` 中相关函数执行屏障；不能将其当作完整缓存行维护或远端 I-cache 广播。GMAC DMA 使用专用非缓存区，不能直接套用普通缓存堆地址。

### 系统调用约束

**Smart 用户态系统调用编号以仓库官方 `components/lwp/lwp_syscall.c` 的表为准，不私自修改或重新编号。** 参数使用 `a0`～`a6`，编号使用 `a7`，通过 `syscall 0` 进入架构分发。

`syscall_c.c` / `syscall_gcc.S` 处理架构入口。`syscall_kernel.c` 中出现的 Linux `SYS_*` 是内核所链接 musl 的内核态调用适配，不是 Smart 用户态编号表。普通 Linux musl ELF 不能因此直接在 Smart 下运行。`kernel.specs` 负责内核 libc 的部分符号绑定。

`examples/test/loongarch_user_test.c` 使用官方表的编号，配合 `loongarch_user_entry.S` 构建独立静态测试程序。上游表变化后应核对测试包装器，不能为了测试方便改变内核 ABI。

### 两款芯片的共享边界

| 项目 | LS2K1000LA | LS2K0300 |
| --- | --- | --- |
| UART0 / 步长 | `0x1fe20000` / `0x100` | `0x16100000` / `0x400` |
| LIOINTC 基址 / 路由值 | `0x1fe01400` / `0x11` | `0x16001400` / `0x10` |
| GMAC 资源 | 固件分配的 PCI BAR | 固定 MMIO `0x16020000`、`0x16030000` |
| GMAC IRQ | 12、14 | 49、50 |
| 内核链接地址 | `0x9000000098000000` | `0x9000000000200000` |

共用 GMAC 分三层：`gmac.c` 管寄存器、PHY、描述符和环；`drv_gmac.c` 管 RT-Thread/lwIP、锁、通知及恢复；板级代码通过 `ls2k_gmac_platform.h` 提供资源和 DMA 地址检查。DMA 区 `.gmac_dma` 不属于 BSS、堆或 ELF 初始化加载段，使用非缓存别名访问；交出描述符 OWN 前需发布数据并执行屏障。

2K0300 不访问 2K1000LA 专有 BOUNCE/AUTO 寄存器，使用传统中断通路。DDR、PLL、引脚复用、外部 PHY 复位和 RGMII 延迟依赖固件；不要在共用驱动里猜测板级连接。

2K0300 参数来源已记录在 [LS2K README](../bsp/loongarch/ls2k1000/README.md)：龙芯官网《龙芯 2K0300 处理器用户手册》V1.1（2026-08），重点为第 2 章、3.2、3.4、3.5、5、14 章。继续修改寄存器时应重新打开该官方手册核对，不能只依赖本文地址表；手册 PDF 不提交仓库。

## 4. 本机工具与环境

工具环境记录于 2026-10-20；2026-10-22 重新核实 GCC、GDB、QEMU 版本及 FAT 工具路径：

| 工具 | 路径或状态 |
| --- | --- |
| LoongArch musl GCC | `~/local/loongarch64-linux-musl-gcc-15.2.0/bin/loongarch64-linux-musl-gcc`，15.2.0 |
| LoongArch GDB | 同一 `bin` 下 `loongarch64-linux-musl-gdb`，16.3 |
| QEMU，支持硬件 PTW | `~/local/qemu-la64/bin/qemu-system-loongarch64`，10.2.0 |
| 另一套 QEMU | `~/local/qemu-loongarch64-10.1.2/bin/qemu-system-loongarch64`；不要用它验证硬件 PTW |
| SCons / Python / 宿主 C 编译器 | `/usr/bin/scons`、`/usr/bin/python3`、`/usr/bin/cc` |
| Python 模块 | `kconfiglib`、`pexpect` 可导入 |
| FAT 工具 | `/usr/sbin/mkfs.fat` 存在；`mcopy` 不在 PATH 中，但 `/tmp/la-linux-refactor/tools/usr/bin/mcopy` 存在，可用 `MCOPY` 显式指定 |

建议在执行本页命令的 shell 中设置：

```sh
export RTT_EXEC_PATH="$HOME/local/loongarch64-linux-musl-gcc-15.2.0/bin"
export RTT_CC_PREFIX=loongarch64-linux-musl-
export QEMU="$HOME/local/qemu-la64/bin/qemu-system-loongarch64"
export MCOPY=/tmp/la-linux-refactor/tools/usr/bin/mcopy
export PATH="$RTT_EXEC_PATH:/usr/sbin:$PATH"
```

工具链可以用 `rg --files "$HOME/local" -g '*musl-gcc' -g '*gdb' -g 'qemu-system-loongarch64'` 定位。自动测试优先用 `RTT_EXEC_PATH`；未设置时扫描 `~/local/*/bin/loongarch64-linux-musl-gcc`，因此多套工具链并存时建议显式设置。

标准版/Smart 自动回归还需要 mtools 的 `mcopy`。此前 CPU 重构验证将 mtools 4.0.43 临时解包到 `/tmp/la-linux-refactor/tools`，未安装到系统 PATH。后续会话应重新确认该临时路径可执行；若已清理，可按本机管理方式安装 `mtools`，或将 `MCOPY` / `--mcopy` 改为实际安装路径。不要把工具二进制加入仓库。

```sh
command -v scons python3 cc mkfs.fat
test -x "$MCOPY"
python3 -c 'import kconfiglib, pexpect'
"$RTT_EXEC_PATH/loongarch64-linux-musl-gcc" --version
"$RTT_EXEC_PATH/loongarch64-linux-musl-gdb" --version
"$QEMU" --version
```

宿主模型需要 C 编译器支持 ASan/UBSan。不要误将交叉编译器设为宿主模型的 `CC`；GMAC 脚本可用 `--cc cc` 指定。

## 5. 配置、编译与运行

所有配置头必须从 `.config` 生成：使用 `scons --menuconfig` 或 `scons --pyconfig-silent`，**禁止手工修改 `rtconfig.h`**。下面复制 defconfig 的命令会替换当前 BSP 配置；有未提交定制配置时先保存副本，完成验证后恢复。

### QEMU

```sh
cd /home/airxs/user/aicode/kernel/rt-thread/bsp/qemu-virt64-loongarch
cp nano_defconfig .config
scons --pyconfig-silent
scons -j8
./run.sh
```

切换版本时把 `nano_defconfig` 换成 `standard_defconfig` 或 `smart_defconfig`。三个 defconfig 默认 SMP 4 核；通过 menuconfig 调整 `RT_USING_SMP`、`RT_CPUS_NR`，Smart 页表模式选择 `LOONGARCH_SOFTWARE_PTW` 或 `LOONGARCH_HARDWARE_PTW`。硬件 PTW 仅在 QEMU 相应能力开启时可选。

输出主要为 BSP 内的 `rtthread.elf`、`rtthread.bin`、`rtthread.map`。调试使用与运行镜像配套的 ELF。构建还会产生配置、链接辅助文件及可能的编译数据库，不提交这些生成物。

`run.sh` 从当前 `rtconfig.h` 读核数和页表模式，使用 `virt`、256 MiB RAM；硬件 PTW 自动传 `-cpu la464,ptw=on`。可设置 `KERNEL`、`QEMU_DISK`、`QEMU_CPU`，但替换 ELF 时要保证脚本读取的配置与 ELF 匹配。

Nano 不需要磁盘。标准版/Smart 首次启动会创建 64 MiB FAT `sd.img`，也可用 `QEMU_DISK` 指定绝对路径。镜像通过 generic loader 装入物理地址 `0x0c000000`，该内存从页分配区排除；客体写入不会在退出时回写镜像。交互终端可用 QEMU 的 `Ctrl-a x` 退出。

现有可选 VirtIO MMIO 驱动要求源平台特定的地址和 IRQ，主线 LoongArch `virt` 不提供这些总线；不要打开该选项来修复 RAM 磁盘或网络问题。

### LS2K1000LA

```sh
cd /home/airxs/user/aicode/kernel/rt-thread/bsp/loongarch/ls2k1000
cp network_defconfig .config
scons --pyconfig-silent
scons -j8
```

`standard_defconfig` 是不启用网络的基础配置。`network_defconfig` 启用双网口、lwIP、DHCP、ping 和 SAL。需要 U-Boot 镜像时显式设置支持 LoongArch 的 `MKIMAGE`；固件需完成 DDR、时钟、引脚和 PCI BAR 分配。不能用 QEMU `virt` 运行这个 ELF 来代替板级测试。

### LS2K0300

```sh
cd /home/airxs/user/aicode/kernel/rt-thread/bsp/loongarch/ls2k0300
cp smart_defconfig .config
scons --pyconfig-silent
scons -j8
```

还可选 `nano_defconfig`、`standard_defconfig`、`network_defconfig`。Smart 默认临时根文件系统，重启丢失内容。控制台由 `BSP_UART_INDEX` 选择 UART0～9，默认 UART0/115200/8N1，必要时用 `BSP_UART_CLOCK_HZ` 覆盖时钟。`BSP_RAM_END` 必须与实板内存及固件保留区一致。

## 6. 自动测试和压力测试

下列 Python 命令在仓库根目录执行。每次使用独立的 `/tmp` 输出目录，便于匹配配置、镜像和日志；需要长期保留时另存到仓库外的持久目录。`/tmp` 不能作为下一次会话一定存在的证据。

### QEMU 回归

```sh
cd /home/airxs/user/aicode/kernel/rt-thread

# 关闭 SMP，依次验证三种版本。
python3 examples/test/loongarch_qemu_test.py --mode all --cpus 0 \
    --output /tmp/la-up

# 四核完整回归及压力；先配置 MCOPY，默认顺序运行各版本客体。
python3 examples/test/loongarch_qemu_test.py --mode all --cpus 4 \
    --smp-iterations 1000000 --iterations 20000 --fork-loops 1024 --repeats 3 \
    --output /tmp/la-smp4

# Smart 硬件 PTW，明确使用 QEMU 10.2。
python3 examples/test/loongarch_qemu_test.py --mode smart --cpus 4 --ptw hardware \
    --output /tmp/la-smart-ptw4

# 十六核软件路径：用于复现待处理问题，不能预先视为通过。
python3 examples/test/loongarch_qemu_test.py --mode smart --cpus 16 --ptw software \
    --smp-iterations 1000000 --iterations 20000 --fork-loops 1024 --repeats 3 \
    --output /tmp/la-smart16
```

`--cpus 0` 关闭 SMP，`--cpus 1` 是单核 SMP，二者应分别验证；其余可选 2、4、8、16。用 `--mode nano` 可在缺少 mcopy 时运行 Nano 测试。`--parallel` 仅在构建完成后并行运行不同版本客体，不允许多个脚本同时修改同一 BSP 配置。

本机后续完整矩阵默认顺序运行客体；若使用自定义批量驱动，最多同时运行两个。此前多客体并发曾出现官方 tick 精确相等断言失败，限制到两个客体后复测通过，但尚未证明宿主负载是唯一原因；保留失败记录，不修改测试断言。这里的客体并发限制不减少单个 Smart 客体内的 `parallel 4 1024` 进程压力。

脚本先替换 `.config`，启用自测及官方 utest，构建 ELF，必要时构建用户测试程序和 FAT 镜像，然后检查：

- 串口输入序号、`port_test`、官方内核 utest；官方定时器压力本身包含 180 秒测试，不要仅因短期无输出就判为挂死。
- `port_unaligned_test` 直接调用模拟器，覆盖 28 种指令、32 个目标寄存器、正负值及非对齐偏移，共 12,880 个数据访问用例；另检查拒绝的指令、零长度操作及 C/汇编帧布局。这些检查不依赖 CPU 是否原生支持非对齐访问，也不表示已启用 ALE 软件修复。
- SMP 每核计数、原子操作、自旋锁、IPI、tick、线程迁移、FPU、线程退出及远端删除回收。
- Smart 官方 MM/LWP 测试、实际 PTW 状态、用户读写及非法指针、信号、浮点状态、TLS/clone、RT IPC、fork/CoW/wait/exec、进程异常隔离。
- Smart SMP 关中断的 TLB 映射切换及远端失效，顺序和并行 fork/CoW/exec 压力；每轮比较堆用量与空闲页数。

当前脚本期望官方测试计数：Nano/标准版 UP 为 19，SMP 为 23；Smart UP 为 32，SMP 为 36。上游测试集合变化时要核对期望数量及实际输出，不能简单减少数量或删除失败匹配。

输出目录含 `<mode>-build.log`、`<mode>-qemu.log`、`<mode>.elf`，非 Nano 还含 `fat.img`、`user.elf`、`user-build.log`。脚本在 `finally` 恢复原 `.config` 并生成 `rtconfig.h`，记录 `restore-config.log`；**不会按恢复后的配置重建 BSP 内的 `rtthread.elf`**。测试后直接运行 `run.sh` 前应重新执行 `scons -j8`，或者使用输出目录中已知配置的 ELF。强制杀进程可能绕过恢复，应检查配置差异。

### CPU/LS2K 宿主检查和交叉构建

```sh
python3 examples/test/ls2k_platform_test.py
python3 examples/test/loongarch_exception_test.py
python3 examples/test/loongarch_port_state_test.py
python3 examples/test/loongarch_page_fault_test.py
python3 examples/test/ls2k_gmac_test.py --build --bsp ls2k1000 \
    --output /tmp/ls2k1000-check
python3 examples/test/ls2k_gmac_test.py --build --bsp ls2k0300 \
    --output /tmp/ls2k0300-check
```

`ls2k_platform_test.py` 验证两种芯片的 UART、时钟、中断路由/确认/屏蔽和 DMA 通路，临时目录自动删除。GMAC 脚本默认先跑 ASan/UBSan 宿主模型，验证 MDIO 超时、PHY、环回绕、OWN 顺序、坏帧和 lwIP 通知失败重试，两个端口模型各 100,000 帧；`--build` 再跑所选 BSP 的构建矩阵。仅测模型时省略 `--build`。

`loongarch_exception_test.py` 用宿主 C 编译器和 ASan/UBSan 检查实际同步异常 C 分发函数：64 个异常码、3 种其他 `ESTAT` 位组合，共 192 例，验证处理函数选择及 BADV 读取/保存范围。它不替代 QEMU 的真实异常入口测试；Smart 用户态回归另覆盖特权指令、非法指令、断点和边界检查异常的进程隔离。

`loongarch_port_state_test.py` 提取实际 MMU 和 syscall C 实现，用 ASan/UBSan 检查 19 个页表分配失败、跨 2 MiB/1 GiB 边界替换及回收用例，并检查地址空间切换的 IRQ 保护、硬件页表查询和 Smart/非 Smart DFS 的 syscall IRQ 状态。模型替换页分配器、CSR 和地址别名，不验证真实指令或内存屏障时序，需配合 QEMU 回归。

`loongarch_page_fault_test.py` 提取实际 `do_page_fault()`，在 ASan/UBSan 下检查 Smart/非 Smart、UP/SMP 共 54 个用例，覆盖可修复缺页、不可调度上下文、空地址空间、IRQ 状态、用户退出及原始异常帧。调度、CSR 和缺页修复器使用宿主替身；真实入口行为另由 QEMU 回归和第 8 节的受控故障对照验证。

2K1000 矩阵为 6 种配置，2K0300 为 10 种，包含网络边界配置、SMP/UP，后者还包含 Nano、Smart、UART9 和 Smart 双网口。脚本检查配置生效、编译告警以及 DMA 段布局。它同样会恢复配置，但不重建恢复后的 ELF。模型不模拟电气与总线时序，也不代表真实吞吐。

### 实板网络入口

menuconfig 启用 `BSP_GMAC_USING_ECHO_TEST` 后重建。在板上执行：

```text
ifconfig
gmac_status
gmac_echo 120
```

主机执行，地址换成板卡实际地址：

```sh
python3 examples/test/ls2k_gmac_test.py --peer 192.168.1.20 --frames 10000 --window 32
```

UDP 端口为 9000，逐帧检查序号和内容。两个网口建议使用独立子网分别测，并核对 `gmac_status` 的实际端口计数。验收还需拔插网线、各速率协商、长期收发、错误计数及内存检查。

## 7. 调试方法

### GDB 连接 QEMU

先按需要构建对应配置，终端一：

```sh
cd /home/airxs/user/aicode/kernel/rt-thread/bsp/qemu-virt64-loongarch
./qemu-dbg.sh
```

脚本是 `run.sh -S -s` 的包装，暂停 CPU 并监听 GDB 1234 端口。终端二：

```sh
cd /home/airxs/user/aicode/kernel/rt-thread/bsp/qemu-virt64-loongarch
"$RTT_EXEC_PATH/loongarch64-linux-musl-gdb" rtthread.elf
```

```gdb
set pagination off
target remote :1234
break rt_hw_board_init
continue
info registers
bt
info threads
thread apply all bt
```

按问题选择 `rtthread_startup`、`loongarch_secondary_start`、`do_page_fault`、`rt_dispatch_syscall`、`loongarch_tlb_flush_all` 等断点。异常向量会复制到运行时地址，直接在模板符号上下断点不一定命中；优先断在其调用的 C 处理函数或根据实际向量地址下断点。

GDB 的线程通常对应 QEMU vCPU，不等于所有 RT-Thread 线程。停滞时先 `Ctrl-c` 停住客体，采集 `thread apply all bt`、各核 PC/SP/寄存器、相关全局状态；不要一开始就重启丢失现场。保存 GDB 输出可使用：

```gdb
set logging file /tmp/loongarch-gdb.log
set logging overwrite on
set logging enabled on
thread apply all bt
thread apply all info registers
set logging enabled off
```

当前调试构建仍为 `-O2 -ggdb`，变量可能被优化掉；结合反汇编、寄存器及结构体检查，不能把 `<optimized out>` 当作状态异常。对 TLB 等时序问题，断点本身可能影响复现。

### 内核命令与镜像检查

启用 `BSP_USING_SELFTEST` 后可在 msh 中逐项执行；Smart/SMP 专用命令需对应配置，`utest_run` 还需启用官方测试集合（自动脚本会配置）：

```text
port_test 20000
port_unaligned_test
smp_test 1000000
smp_reap_test 1000
port_mmu
smp_tlb_test 10000
port_resources
list thread
utest_run
```

Smart FAT 镜像内有测试程序时，还可执行 `/user.elf`、`/user.elf stress 1024`、`/user.elf parallel 4 1024`。自动脚本产出的 `fat.img` 已含该程序；手动运行可用 `QEMU_DISK=/tmp/la-smp4/fat.img` 指定，确保所运行的内核是 Smart。

`port_unaligned_test` 当前接入 QEMU BSP 的 `BSP_USING_SELFTEST`，两款 LS2K BSP 未添加此命令。

在 BSP 目录下可检查镜像：

```sh
"$RTT_EXEC_PATH/loongarch64-linux-musl-readelf" -h -lW rtthread.elf
"$RTT_EXEC_PATH/loongarch64-linux-musl-nm" -n rtthread.elf > /tmp/loongarch-symbols.txt
"$RTT_EXEC_PATH/loongarch64-linux-musl-objdump" -dS rtthread.elf > /tmp/loongarch-disasm.txt
```

异常 PC 可用同工具链的 `addr2line -e rtthread.elf -f -C 地址` 定位，必须使用产生该异常的 ELF。启动和 DMA 故障还需核对 `rtthread.map`、链接脚本、ELF 加载段和缓存/物理地址转换。

### 分析停滞、泄漏和异常的顺序

1. 固定版本、配置、工具链、QEMU、核数、PTW、测试次数及命令顺序；保存 ELF 和串口日志。超时只是现象，先确认最后一个完成标记。
2. 分开检查单项和组合：启动、内核测试、线程回收、TLB、用户态回归、顺序 fork、并行 fork。保留失败时的原始组合，不能只用更轻的测试代替。
3. SMP/TLB 停滞时查看 `smp.c` 的 `online_mask`、`tlb_request`、`tlb_ack`、`tlb_sequence`，找出哪个核在等待哪个确认，再结合锁持有者及 IRQ 状态判断。
4. 区分测试屏障错误和内核错误。`tlb_worker()` 必须让全部工作线程离开 `rt_thread_mdelay()` 后才关闭各核中断；否则 CPU0 的 tick 停止，其他核仍在等延时唤醒。已有 `tlb_test.ready` 屏障，不要删除。
5. 进程退出后的资源回收依赖低优先级 `tsystem`。`waitpid` 返回不意味着所有内存已回收；现有压力测试分批暂停让回收线程运行。比较同一次启动、完成回收后的资源基线，检查 PID 槽位上限，不能直接把资源暂占判为泄漏，也不能用无限延长等待掩盖问题。
6. 用户态异常核对 `ERA`、`BADV`、异常码、栈帧、页表权限及用户地址空间；内核异常额外核对当前 IRQ 状态和中断嵌套。系统调用问题先辨别用户 Smart ABI 与内核 musl 适配，绝不尝试改编号绕过。

## 8. 历史验证与阶段提交

以下为 2026-10-14 已写入 BSP README 的验证记录，**不是 2026-10-20 重新运行的结果**：

| 配置 | 官方 UP 测试 | 官方四核 SMP 测试 | 压力记录 |
| --- | --- | --- | --- |
| Nano、标准版（各自） | 19/19 | 23/23 | 各 3 轮、每工作线程 20,000 次 |
| Smart 软件 refill | 32/32 | 36/36 | 同上；顺序 fork/CoW 3×1,024；四核并行 3×4×1,024 |
| Smart 硬件 PTW | 32/32 | 36/36 | 同上；对应用户压力资源基线一致 |

补充记录：Nano/标准版 1、2、8、16 核每核 1,000,000 次 SMP 校验及 1,000 轮线程回收；Smart 软件相应核数的 SMP 校验及 10,000 轮 TLB 测试，硬件四核 10,000 轮 TLB 测试；Smart 软件八核另通过 3×8×1,024 并行 fork/CoW/exec。十六核组合长压仍保留未通过状态。

两款 LS2K 共 6/10 种配置交叉构建通过；ASan/UBSan 下共用 GMAC 两端口模型各 100,000 帧，两种串口模型各 100,000 次轮询收发。UDP 客户端曾通过本机回显校验，不能算实板结果。

最近的注释阶段 `eac8b3f8eb` 修改 18 个文件，代码 token 对比确认只改注释；重新编译了 Nano 四核、Smart UP/四核软件 refill、Smart 四核硬件 PTW、两款 LS2K 默认配置。该阶段未重跑 QEMU 或压力测试。

可用 `git show <提交>` 查看阶段依据：

| 提交 | 内容 |
| --- | --- |
| `a904318968` | 增加 LS2K0300 Nano/Smart，共用 LS2K 驱动 |
| `8f543750c7` | LoongArch SMP 启动、调度、TLB shootdown |
| `572f7031a3` | SMP 线程回收、串口接收顺序修复 |
| `2527ed6cac` | UART、IRQ、GMAC 的 SMP 保护 |
| `e7ff9a6671` | Smart 硬件 PTW、异常 IRQ 状态修复 |
| `337b22ecde` | 信号唤醒与调度状态检查同步 |
| `2aeef80712` | BSP SMP 默认配置、多核压力覆盖 |
| `11cb1cdc0d` | 八核长压记录及十六核待定位说明 |
| `c5a8816f3e` | TLB 测试进入关中断阶段前的就绪屏障 |
| `5284f2410f` | LS2K 共用外设移入 `bsp/loongarch/common` |
| `daed02d0a9` | 中断文件改名 `platform_interrupt` |
| `c3d8a8b2f4` | BSP 改名 `ls2k1000` |
| `eac8b3f8eb` | 核心函数 RT-Thread 风格注释及约束说明 |

历史 `/tmp` 日志或辅助脚本可能已经被清理。本次没有找到此前十六核调试的结果文件，不能把当时后台任务的中途进度记为最终通过。后续应使用仓库内测试入口重新复现，保存可追溯记录。

### 2026-10-20：CPU 实现等价重构

代码提交：`1e43f960c9`，基于 `454d86af1c`。先比对本地参考树 `/home/airxs/user/aicode/system/linux/linux-6.11-labcore/arch/loongarch`，确认重复主要位于非对齐模拟、指令描述、寄存器定义及异常帧宏，再进行如下调整：

- 用统一访存描述表替代三组位域解码分支，保留 28 种指令及原有 GPR/live FPR 语义；字节读取仍由高地址到低地址，写入仍由低地址到高地址。
- 用同一寄存器组描述展开异常帧、线程切换帧和浮点状态的保存/恢复；C 结构体与汇编偏移保持不变。三个页故障入口共用处理体，主核/次核共用初始化宏。
- 将硬件定义移入 `loongarch_regs.h`，`loongarch.h` 保留访问接口，合并 ECFG 和本地 IRQ 的重复操作。硬件编码、原有常量宏、`regdef.h`、`fpregdef.h` 保持兼容。
- 按用户要求清理代码中的来源描述注释，保留已有版权和 SPDX 标识。`do_ale()`、系统调用编号、SMP/TLB 协议和 MMU 算法未改变。

本次实际验证使用 GCC 15.2.0、QEMU 10.2.0：

| 配置 | UP 官方测试 | 四核官方测试 | 内核压力 |
| --- | --- | --- | --- |
| Nano | 19/19 | 23/23 | 每种配置 3 轮，每工作线程 20,000 次 |
| 标准版 | 19/19 | 23/23 | 每种配置 3 轮，每工作线程 20,000 次 |
| Smart 软件 refill | 32/32 | 36/36 | 每种配置 3 轮，每工作线程 20,000 次 |
| Smart 硬件 PTW | 32/32 | 36/36 | 每种配置 3 轮，每工作线程 20,000 次 |

八组均通过串口顺序检查及 `port_unaligned_test` 的 12,880 个数据用例、11 个不支持指令检查、零长度操作和帧布局检查。数据用例在重构前也通过。宿主 ASan/UBSan 差分检查比较了新旧解码器的 8,388,608 个指令字，结果一致；预处理宏检查确认原硬件定义保留，主核/次核启动代码的指令字节一致。

四核各组均通过每核 1,000,000 次 SMP 校验及 1,000 轮线程回收；Smart 四核两种 PTW 各通过 3×1,000 轮 TLB 测试。四组 Smart 均通过用户态回归及 3×1,024 次顺序 fork/CoW，四核两组另通过 3×4×1,024 次并行 fork/CoW/exec。所有轮次资源计数与各自启动的基线一致：UP 为堆 92,896 字节、空闲页 31,603/31,690；四核为堆 119,296 字节、空闲页 31,582/31,669。

执行 `/tmp/la-linux-refactor/run_matrix.py` 调用了仓库测试脚本的 `build()` / `exercise()`：串行构建八个独立 ELF，再并行运行客体。测试入口、次数及检查未削减。可用以下仓库命令复现同一配置集合（先按第 4 节设置工具路径）：

```sh
for cpus in 0 4; do
    python3 examples/test/loongarch_qemu_test.py --mode all --parallel --cpus "$cpus" \
        --smp-iterations 1000000 --iterations 20000 --fork-loops 1024 --repeats 3 \
        --output "/tmp/la-refactor-${cpus}-software"
    python3 examples/test/loongarch_qemu_test.py --mode smart --cpus "$cpus" --ptw hardware \
        --smp-iterations 1000000 --iterations 20000 --fork-loops 1024 --repeats 3 \
        --output "/tmp/la-refactor-${cpus}-hardware"
done

python3 examples/test/ls2k_platform_test.py
python3 examples/test/ls2k_gmac_test.py --build --bsp ls2k1000 \
    --output /tmp/la-linux-refactor/ls2k1000
python3 examples/test/ls2k_gmac_test.py --build --bsp ls2k0300 \
    --output /tmp/la-linux-refactor/ls2k0300
```

两款 LS2K 的 6/10 种配置均交叉构建通过，无编译告警；平台模型及 ASan/UBSan GMAC 模型通过。三个 BSP 的 `.config`、`rtconfig.h` 均已恢复。日志、配置快照、ELF、基线源码及差分结果位于仓库外 `/tmp/la-linux-refactor`，最终汇总为 `summary.json`；这些临时文件可能被清理，不能作为以后会话必然存在的证据。

本轮未重测 1/2/8/16 核，未进行实板验证或完整 Smart libc 应用验证。第 2 节所列十六核组合长压问题继续保留，不能据本次四核通过判定已解决。

### 2026-10-20：汇编别名与异常入口重构

代码提交：`17928ab622`，基于 `d8bb26c11b`。按用户要求将七个 `LONG_*` 指令别名改为 `ADD`、`ADDI`、`STUB`、`LD`、`ST`、`FLD`、`FST`，同步修改使用处，当前仍映射原 64 位指令。没有加入 32 位实现，`regdef.h`、`fpregdef.h` 未修改。

同步故障向量共用一份汇编保存入口，由 `trap.c:loongarch_dispatch_exception()` 读取已保存的异常码分发；原向量符号保留为入口别名。IRQ 延迟调度、Smart 用户返回和页故障直接恢复分别保持原策略，本地中断开关共用 CRMD.IE 更新操作。向量桩仍为 40 字节、指针偏移 32，软件 refill 和帧 ABI 保持不变。注释描述当前行为，未新增来源描述。

本阶段实际检查：

- 宿主 ASan/UBSan：`python3 examples/test/loongarch_exception_test.py` 通过 192 个异常码/状态位组合，检查 C 分发及 BADV 更新范围。
- Nano UP、Smart 四核的软件 refill 配置中，单独应用宏改名时，公共汇编预处理结果保持一致。完成异常重构后，目标文件对比确认上下文切换、系统调用等未改变指令字节，向量桩和软件 refill 字节一致，入口别名地址与大小正确。异常汇编 `.text` 分别从 5,460/5,404 字节缩减为 1,972/1,916 字节。
- 新增 `asrtle.d` 用户边界检查异常用例，在重构前后的 Smart 四核软件内核均产生 `Bounds check error`，故障子进程退出，其余回归继续通过。
- 两款 LS2K 的 6/10 种配置交叉构建及脚本附带的 ASan/UBSan GMAC 模型均通过，无编译告警。三个 BSP 的 `.config`、`rtconfig.h` 已恢复。

使用 GCC 15.2.0、QEMU 10.2.0 重新运行八组完整矩阵，参数与上一阶段相同：UP/四核，Nano/标准版/Smart 软件 refill/Smart 硬件 PTW；内核压力每线程 20,000 次、3 轮；顺序 fork 3×1,024，四核并行 fork 3×4×1,024。调用入口为 `/tmp/la-exception-refactor/run_matrix.py`，仍使用仓库 `loongarch_qemu_test.py` 的 `build()` / `exercise()`，没有删减测试。

首轮结果为 **7 组完整通过、1 组失败**。八组官方测试均通过（Nano/标准版 UP 19、四核 23；Smart UP 32、四核 36），非对齐专项均通过 12,880 例；四核 SMP 校验每核 1,000,000 次、线程回收 1,000 轮，Smart 四核 TLB 各 3×1,000 轮均通过。Smart 四核硬件 PTW 已通过三轮顺序 fork、两轮并行 fork，但第三轮并行中出现 `Kernel page fault: pc=0x90000000002659a0 address=(nil)`，尚未进入三轮内核压力。对应 ELF 定位到 `src/klibc/kstring.c:rt_strcmp()` 第一条读取 `a0` 的指令；其他七组完成全部压力，资源计数均与各自基线一致。

为排查此故障，使用相同新用户测试镜像分别运行重构前的 `1e43f960c9` ELF 和本阶段 ELF：`/tmp/la-exception-refactor/reproduce.py` 对每个内核执行十轮顺序 1,024 次及并行 4×1,024 次 fork/CoW/exec，均通过，资源计数保持各自启动基线。该结果不能排除首轮故障，也尚不能确认故障与本次重构的关系。

另用 `/tmp/la-exception-refactor/replay_matrix.py` 对新旧内核各完整重跑一次 Smart 四核硬件 PTW 的 `exercise()`，包含所有官方测试前置步骤、3 轮用户顺序/并行压力及 3 轮内核压力，两者均通过。每轮资源恢复为各自启动基线（堆 119,296 字节、空闲页 31,582/31,669），未再次触发故障。此后未改动运行逻辑，没有将偶发故障标记为修复。

初次矩阵、对照压力、配置、ELF 及汇编对比保存在仓库外 `/tmp/la-exception-refactor`；首轮汇总为 `summary.json`，失败日志仍在 `smart-4-hardware/smart-qemu.log`，不以复测日志覆盖。对照复测汇总为 `retest-summary.json`，完整复测日志位于 `replay-baseline/`、`replay-current/`。LS2K 沿用上一阶段的两条 `--build --bsp` 命令，将 `--output` 分别改为 `/tmp/la-exception-refactor/ls2k1000`、`/tmp/la-exception-refactor/ls2k0300`。未重测 1/2/8/16 核，未进行实板验收；十六核历史问题及上述四核偶发故障继续保留。

### 2026-10-20：四核页故障的后续定位

原始异常只有 `rt_strcmp()` 的 PC 和空地址，没有调用者寄存器，不能确定传入空指针的函数。针对未修改的 `/proc` 实现，临时构建在第 50 个数字 PID 名称分配处注入失败，稳定触发同类内核页故障；保存的异常帧显示返回地址位于 `_proc_find()`，其第一个参数来自目录项的空 `name`。这证明一条真实可达的故障路径，但不能证明最初的偶发异常是分配失败造成。另一个并发风险是创建 PID 的 `/proc` 节点时持有 PID 锁，而退出及回滚删除节点发生在该锁外；宿主并发检查确认删除可与创建重叠。两项问题都在通用组件中，目前没有证据表明改动 LoongArch 异常入口或 MMU 能修复它们。

用户限定不能修改 `components/` 等公共代码，因此排查用的公共源码修改和临时测试已撤回。LoongArch `do_page_fault()` 的内核故障输出补充保存的 `ra`、`a0`、`a1`，以便下次复现时区分 `/proc` 路径与其他 `rt_strcmp()` 调用点。内核访问空地址时直接走现有致命异常路径；地址零不在该移植的有效用户区，不能由通用缺页修复器修复。这是故障报告及处理路径的改动，**不是原始空指针来源的修复**；四核硬件 PTW 偶发故障仍列为未解决。

仅增加寄存器输出时，宿主异常分发检查 192 例通过，Smart 四核硬件 PTW 的 QEMU 回归完成：官方测试 36/36、每核 100,000 次 SMP 校验、1,000 轮线程回收、1,000 轮 TLB 测试、128 次顺序及 4×128 次并行 fork/CoW、每工作线程 2,000 次内核压力均通过，资源计数回到堆 119,296 字节及空闲页 31,582/31,669。该规模低于原始故障的 4×1,024，不能替代故障复现。BSP `.config` 和 `rtconfig.h` 已恢复。

追加的原规模 4×1,024 并行压力在前五轮资源稳定后，第六轮出现另一种空地址故障。GDB 从异常帧定位到 `lwp_aspace_switch()`：目标线程的 `lwp` 指针非空，但 `lwp->aspace` 为零，且此时处在调度切换中。旧页故障路径先进入通用修复器并报“scheduler is not available”，遮盖了故障 PC；上述内核空地址直达致命异常路径的调整针对的正是这一诊断问题。这次并非最初 `rt_strcmp()` 的 PC，也不能据此确定两次故障有同一根因。镜像、串口及 GDB 记录在 `/tmp/la-null-fault/port-diagnostic-stress`；记录目录在仓库外。

加入空地址直达报告后重新构建 Smart 四核硬件 PTW 镜像，宿主异常分发检查仍为 192/192。原规模压力在前五轮资源稳定后，第六轮首次测得未回收资源；GDB 显示系统线程仍在执行 `lwp_free()`，因此不能把这次快照判为泄漏。允许资源回收后再比较的复跑在第 2 轮出现互斥量对象类型断言，其后回溯器访问地址 `0x10` 并触发次生页故障。两次复跑都未捕获最初的 `rt_strcmp()` 调用者。新镜像、构建及串口记录位于 `/tmp/la-null-fault/kernel-null-direct`；BSP 配置已恢复。上述压力失败仍待分别定位，不能把诊断改动记为修复。

同一最终源码还完成 Nano 单核配置交叉构建、QEMU 启动及 `port_test 200`。在 Smart 四核 QEMU 的独立调试会话中，GDB 于 `proc_dump /1` 的 `rt_strcmp()` 入口人为将 `a0` 置零，内核按预期输出故障 PC、空地址、`_proc_find()` 内的返回地址及 `a0=(nil)`，验证了新的空地址故障报告；这是受控注入，不是自然故障复现。相关记录保存在同一仓库外目录。

继续定位时，Smart 四核硬件 PTW 临时关闭 `RT_USING_DFS_PROCFS`，未改 `components/` 源码，用户态回归可通过，但并行 fork 压力仍会触发 `_rt_mutex_take()` 的对象类型断言。断言处 GDB 显示，仍在 CPU 上运行的用户线程所属 LWP 已有 `ref=0`、`pid=0`、`aspace=NULL`，其 `lwp_lock` 类型也已被清零。这说明该次断言和先前 `lwp_aspace_switch()` 空地址故障都能由活动线程的 LWP 被提前释放解释，且这条故障链不依赖 procfs。

在未修改的 `lwp_pid_put()` 中设置仅命中 `pid==0` 路径的指令断点，又捕获一次重复释放：`lwp_thread_exit()` 经 `lwp_terminate()` 调用 `lwp_pid_put()` 时，传入 LWP 已是 `pid=0`、`ref=1`，线程还在其线程组中。该函数即使未找到 PID 仍继续执行 `lwp_ref_dec()`，使最后一个引用归零；随后地址空间和互斥量被释放。结合 `_stats_and_reap_child()` 可在子进程完成 `_resr_cleanup()` 前先回收 PID、解除父子关系，而 `_resr_cleanup()` 在观察到 `parent=NULL` 时也调用 `lwp_pid_put()`，存在明确的重复释放时序。最小正确修复需要在通用 LWP 的 PID 回收处以 PID 锁保护“仅首次释放才递减引用”的判断，并维持 `/proc` 节点与 PID 的同步；不能在 LoongArch 缺页或上下文切换路径中跳过损坏对象来替代该修复。用户要求不修改 `components/` 等公共代码，因此目前只记录证据，**未修复这项生命周期故障**。它是否也是最初 `rt_strcmp()` 故障的根因尚未证实。

本轮调试日志和 ELF 位于仓库外 `/tmp/la-null-fault/no-procfs-diagnostic/`，其中 `pid0-break-gdb.log` 为重复释放现场，`assert-details-gdb.log` 为活动线程使用已释放 LWP 的现场。另构建 Smart 四核软件 refill 运行一轮 4×1,024 并行压力，运算完成，但堆占用比该次启动基线多 2,608 字节，等待 20 秒后未恢复；此观察不能直接证明持续泄漏，记录于 `/tmp/la-null-fault/software-diagnostic/`。两次构建后 BSP `.config` 和 `rtconfig.h` 均已恢复，未进行完整矩阵或实板验收。

### 2026-10-20：LoongArch MMU 和 syscall 状态修复

本阶段基于 `b6d1dfa1da`，仅修改 `libcpu/loongarch/common/mmu.c`、`syscall_c.c`，增加架构专项宿主测试并更新本文；未修改 `components/`、`src/` 或公共头文件。此前发现的非 16 字节栈顶下 fork/clone 帧定位问题位于 `components/lwp/arch/loongarch`，本阶段未改；FPU 禁用时异常保存可能再次触发异常的静态审计项也未纳入本次修复。

- `rt_hw_aspace_switch()` 在函数内部保存并关闭本地 IRQ，完成 PGDL 写入和本地 TLB 失效后恢复原状态。删除每核软件页表记录，`rt_hw_mmu_tbl_get()` 直接读取 PGDL，避免迁核时更新错误 CPU 的记录。
- 映射先按 2 MiB 叶表范围准备全部所需页表，再写叶子映射。分配失败仅回收空页表，不修改原有叶子项；包含“中间目录已分配、下一层分配失败”的路径。回收仍等待跨核 TLB 失效后再释放页表页。
- Smart 内核态 syscall 和非 Smart DFS syscall 按保存的 PRMD.PIE 设置执行期间的 IRQ 状态，返回前关闭 IRQ。用户态 syscall 编号、参数和返回路径保持不变。

定向检查：

```sh
python3 examples/test/loongarch_port_state_test.py
python3 examples/test/loongarch_exception_test.py
```

ASan/UBSan 下 19 个 MMU 分配失败/替换/回收场景、地址空间切换模型、Smart/非 Smart DFS syscall 模型及 192 个同步异常分发用例均通过。将同一 MMU 和 syscall 检查用于未修改的 `b6d1dfa1da` 源码，分别因残留页表和错误打开 IRQ 而失败。QEMU 四核硬件 PTW 的实际 fork、exec 路径通过 GDB 验证：PGDL 写入前 CRMD 为 `0xb0`，失效后 PGDL 等于目标页表，函数返回后 CRMD 恢复为 `0xb4`。

使用 GCC 15.2.0、QEMU 10.2.0，`/tmp/la-port-fix/run_matrix.py` 顺序构建八组 UP/四核配置，再复用 `loongarch_qemu_test.py` 的完整 `exercise()`。八组构建无告警。四组 UP 均完整通过：Nano/标准版官方测试各 19/19，Smart 软件/硬件 PTW 各 32/32；每组三轮内核压力各工作线程 20,000 次，两组 Smart 各三轮 1,024 次顺序 fork/CoW，资源恢复到各自基线。四核首轮在官方线程延时或定时器的 tick 精确相等断言处失败，未将其记为通过；首轮日志保留，后续复测单独记录。

随后 `/tmp/la-port-fix/replay_smp.py` 将独立客体并发限制为两个，使用相同 ELF 完整复测四核官方用例，未修改断言或删除测试。Nano/标准版各 23/23、Smart 软件/硬件 PTW 各 36/36 均通过。首轮时序断言没有再次出现，但未将降低客体并发直接认定为唯一原因。四组随后各三轮、每工作线程 20,000 次内核压力均通过。四核两组 Smart 各三轮 1,000 次 TLB 测试及用户态基础回归通过。所有四核配置还通过每核 1,000,000 次 SMP 校验、1,000 轮线程回收和非对齐专项。

完整前置测试后的进程压力仍失败：软件 refill 完成三轮顺序 1,024 次、两轮并行 4×1,024 次，第三轮并行中发生内核地址异常；硬件 PTW 完成两轮顺序、一轮并行，第二轮并行中再次发生 procfs 遍历停滞。已完成并行轮次的资源均恢复为堆 119,296 字节、空闲页 31,582/31,669。硬件现场有两个 CPU 分别沿 PID 创建和删除路径遍历同一个节点，节点 `next` 指向自身而不是根目录链表头，连续采样保持不变。软件异常帧为 PRMD=`4`、ERA=`0x9000000000265980`（`rt_strcmp`）、RA=`0x9000000000211924`（内联 `_proc_find` 的 `proc_find+548`）、BADV/a0=`0x29c0607a29c08079`，确认本次非法字符串指针来自 procfs 查找路径；尚未捕获最初破坏节点的写操作。其他 CPU 此时停在等待 TLB 确认处，不能仅凭这些等待栈把该次故障归为 TLB 协议死锁。保存现场后终止两个失败客体，第三轮未完成的部分不计为通过。

另外使用新构建的四核 Smart 镜像，各计划 12 轮 `parallel 4 1024` 压力，允许每轮结束后等待 20 秒回收。软件 refill 在第一轮停滞；硬件 PTW 前三轮完成，资源均为堆 78,592 字节、空闲页 31,655/31,669，第四轮停滞。两者都捕获到 `_proc_find()` 反复遍历一个已经自环的 `sibling` 节点，其 `next/prev` 等于节点自身，且该节点不是根目录的链表头；调用链为 `proc_pid()` → `lwp_create()` → `_sys_fork()`。这是本轮自然压力现场，未注入分配失败或修改客体代码。保存多次 GDB 采样后主动终止两个停滞客体，不能把终止产生的 EOF 当作原始故障，也不能把计划的 12 轮记成完成。

该现场确认修复后仍有 procfs 遍历停滞，符合无统一保护的目录遍历/删除发生重叠的风险；未据此宣称已经捕获最初修改链表的指令，也未将此前 PID 重复释放、空地址页故障全部归为同一根因。公共代码保持不变，系统仍未通过多核长时间稳定性验收。

镜像、配置、宿主对照结果、串口日志、GDB 记录及测试脚本均在仓库外 `/tmp/la-port-fix`。首轮汇总为 `summary.json`，四核复测为 `replay-summary.json`；复测故障现场为 `replay-smart-4-software/address-error-gdb.log`、`replay-smart-4-hardware/proc-loop-gdb.log`；扩展压力现场为 `stress-software/proc-loop-gdb.log` 和 `stress-hardware/proc-loop-gdb.log`；实际切换验证为 `verify-fork/gdb.log`、`verify-exec/gdb.log`。QEMU BSP `.config` 和生成的 `rtconfig.h` 已恢复。本阶段未测试 LS2K 实板、1/2/8/16 核 SMP 或完整 Smart libc 应用。

### 2026-10-20：捕获 procfs 遍历与摘除的实际读写链

本轮继续分析 `dbcb780b13` 的压力故障，复用 `/tmp/la-port-fix` 的配套 ELF 和磁盘镜像，没有修改内核、公共组件、架构源码或 BSP 配置。先核对上轮异常帧与 ELF，再通过 QEMU 内存访问跟踪记录 procfs 指令的实际访存，触发后才连接 GDB。跟踪会影响调度时序，但未注入分配失败、改写客体数据或寄存器。逐次 GDB 断点的早期尝试明显拖慢客体，未捕获目标重叠，不能计为压力通过。

**硬件 PTW 已捕获具体写入者和后续读者。** 四核 `/user.elf parallel 4 1024` 完成第一轮，第二轮在创建 PID 8256 时停滞。节点地址为 `0x900000000037f6b0`，其 `sibling` 地址为 `0x900000000037f6d8`。跟踪中连续出现：

| 回调序号 | CPU（从 0 编号） | 指令 | 实际访存 |
| --- | --- | --- | --- |
| 13282939 | 0 | `proc_find+492`，`0x90000000002118ec` | 从前驱 `0x900000000037e848` 读取并缓存下一节点 `0x900000000037f6d8` |
| 13282943 | 1 | `proc_remove_dentry+124` | 将该前驱的 `next` 改为后继 `0x900000000037fae8`，完成摘链 |
| 13282945 | 1 | `proc_remove_dentry+132`，`0x9000000000212604` | 向 `0x900000000037f6d8` 写入自身地址 |
| 13282955 | 0 | `proc_find+516`，`0x9000000000211904` | 沿之前缓存的节点读取 `next`，读到 `0x900000000037f6d8` 自环 |

这些序号表示宿主跟踪回调的记录顺序，并非全部客体访存的全局硬件顺序；结论依据上述普通加载、存储的地址和值。GDB 随后确认 CPU 0 的 `_proc_find()` 中 `dentry` 和 `tmp` 都等于该节点，正在查找 `"8256"`，另外三个 CPU 已进入 idle。该现场定位到 **遍历仍使用已摘除节点**：删除时把节点设为自环是链表删除函数的正常动作，错误在于另一个核仍沿未受生命周期保护的缓存指针继续遍历。这次并不需要先假设根目录链表已损坏，也不需要由 TLB 等待解释停滞。

同样的跟踪用于四核软件 refill，连续六轮 `parallel 4 1024` 均输出 `SMART PARALLEL PASS`，未触发自环或代码区目录项检测。该复测未进行每轮资源基线核对，也不包含完整官方测试；不能据此宣称软件路径稳定或推翻先前软件 refill 的失败记录。两种模式均已结束客体，硬件第二轮未完成的部分不计为通过。

源码和当前 ELF 一致：`_proc_find()` 使用的 `rt_list_for_each_entry_safe` 只预存下一节点，不能保障另一个 CPU 不删除该节点；匹配成功之后才调用 `proc_acquire()`，遍历期间没有取得节点引用。PID 创建在 PID 锁内调用 `proc_pid()`，但 `lwp_pid_put()` 在取得 PID 锁之前就执行 `proc_remove_dentry()`，两条路径可以重叠。当前配置启用 `RT_USING_STDC_ATOMIC`，引用加减已编译为 `amadd_db.d`，不能误报为普通非原子加减；原子计数仍不能自动保护链表遍历、摘除与释放这组操作。

**上轮软件 refill 的非法字符串地址也已进一步解码。** 保存的 `s2=0x9000000000252f48` 实际位于代码区，是 `_anon_varea_expand-40`，不是堆中的目录项。按当前 `struct proc_dentry` 布局，查找器从 `s2+96` 读取 `name`；ELF 中该位置恰为 `_pgmgr_pop_range+8` 的两条指令 `0x29c08079`、`0x29c0607a`，合成的 64 位值精确等于保存的 `a0/BADV=0x29c0607a29c08079`。`s2+40` 读取的代码字再减 40，也精确等于保存的 `s3`。这些关系已用配套 ELF 自动断言验证，说明这份现场与普通链表加载、偏移计算一致。

`rt_private_ctx.mem_obj.on_varea_expand` 与 `proc_dentry.node.sibling.next` 都在各自结构的偏移 40，且前者保存 `_anon_varea_expand`。因此，失效目录项内存被匿名映射对象复用，是上述代码地址进入遍历链的一条具体解释；**原异常帧没有保留前一个堆节点的地址，本轮尚未捕获该次分配/复用过程，不能将此解释写成已证实的释放后使用全过程**。

对上轮“`next` 为自身、`prev` 为根目录头”的另一种现场，另按当前 ELF 中摘链的两次加载、四次存储顺序，枚举两个相邻节点并发删除的 924 种交错。严格顺序一致的内存模型下，94 种交错就能留下这种形态。模型结果不是历史故障的实际写入记录，但证明仅增加 LoongArch 内存屏障不足以排除此风险。

本轮确认的修复边界是 procfs 遍历、增删和引用释放之间的生命周期同步；之前已捕获的 LWP 重复 PID 释放仍是另一项待修问题，不能将两条链合并为同一次故障。用户禁止修改公共代码，因此本轮只补充证据，没有把停滞标记为修复，也没有重新执行完整构建/官方测试矩阵。

调试工具与原始记录保存在仓库外 `/tmp/la-cause-analysis`：`hardware-events.log` 为触发前最后 1,048,576 条访存回调；`hardware-chain.log` 为自动检查通过的上述读写链；`hardware-trace-gdb.log`、`hardware-trace-guest.log` 为对应栈和串口记录；`software-trace-driver.log`、`software-trace-guest.log` 为六轮软件 refill 记录；`saved-frame-verified.log` 为旧异常帧与 ELF 的算术核对；`list-interleavings.log` 为相邻删除模型。可复查命令：

```sh
python3 /tmp/la-cause-analysis/analyze_trace.py hardware
python3 /tmp/la-cause-analysis/list_interleavings.py
```

### 2026-10-20：无访存跟踪的追加压力测试

基于 `a1d91b0b18`，本轮只执行压力测试和故障现场分析，没有修改实现或 BSP 配置。该提交相对 `dbcb780b13` 只有文档变化，因此复用 `/tmp/la-port-fix` 的配套 ELF、配置和磁盘镜像；未重新构建。使用 QEMU 10.2.0，最多同时运行两个独立客体，不加载访存跟踪插件，不预设断点，只在失败后连接 GDB。六组测试实际耗时约 13 分 12 秒，结果为四组完成、两组失败。

| 配置 | 已完成的本轮压力 | 结果 |
| --- | --- | --- |
| Nano 四核 | 三轮内核压力；SMP 校验、线程回收 | 通过 |
| 标准版四核 | 三轮内核压力；SMP 校验、线程回收 | 通过 |
| Smart 四核软件 refill | 三轮内核、三轮 TLB、三轮顺序、七轮并行压力 | 第八轮并行发生调度切换页故障，随后断言 |
| Smart 四核硬件 PTW | 三轮内核、三轮 TLB、三轮顺序、三轮并行压力 | 第四轮并行发生 `rt_strcmp()` 内核页故障 |
| Smart 关闭 SMP，软件 refill | 一轮内核、三轮顺序压力 | 通过 |
| Smart 关闭 SMP，硬件 PTW | 一轮内核、三轮顺序压力 | 通过 |

上述每轮内核压力为两个工作线程各 20,000 次；四组 SMP 配置均通过每核 1,000,000 次校验及 5,000 轮线程自然退出/远端删除（每组创建、回收 20,000 个测试线程）。两组四核 Smart 的 TLB 压力均为三轮各 10,000 次映射切换。六组均通过非对齐专项，四组 Smart 均通过实际 MMU 模式检查及用户态基础回归。每轮顺序压力为 `stress 4096`，四核并行压力为 `parallel 4 1024`，每种模式原计划最多十二轮，遇到失败即保存现场并停止该客体；未完成部分不计为通过。本轮未运行完整官方测试集合。

所有已完成的 Smart 进程压力轮次都在允许最多 30 秒延迟回收后恢复资源基线：四核两组均为堆 78,592 字节、空闲页 31,655/31,669；关闭 SMP 两组均为堆 78,560 字节、空闲页 31,676/31,690。首轮并行结束后曾短暂保留较多资源，后续采样回到基线，未将其计为持续泄漏。

**硬件 PTW 失败现场：** CPU 0 的保存帧为 ERA=`0x90000000002659e0`（`rt_strcmp()` 第一条加载）、BADV/a0=`0x183`、RA=`0x9000000000211924`（`proc_find+548`）、PRMD=`4`。栈中保留 `proc_create()`、`proc_mkdir_data()`、`proc_pid()` 创建 PID 7960 的调用链。两次相隔三秒的快照一致，其他 CPU 在等待故障核的 TLB 确认。该记录确认 procfs 查找再次传入非法字符串地址；本次没有捕获 `0x183` 的写入过程，不能仅凭相似调用链断言与上轮自环完全相同，也不能把其他 CPU 的等待当作独立 TLB 死锁。

**软件 refill 失败现场：** CPU 2 的原始异常帧为 ERA=`0x9000000000246b3c`（`lwp_aspace_switch+28`）、BADV=`0x10`、RA=`0x9000000000285470`（`rt_cpus_lock_status_restore+16`）、PRMD=`0`，目标线程参数为 `0x900000000038c8c0`。配套 ELF 的故障指令为 `ld.d $s1, $s0, 16`，其中基址来自 `thread->lwp->aspace`，偏移 16 对应 `rt_aspace.page_table`，与空地址空间解引用相符；本次切换尚未调用 `rt_hw_aspace_switch()`。随后 `do_page_fault()` 调用通用缺页修复器，因当前正在调度切换而在 `rt_aspace_fault_try_fix()` 第 153 行触发“scheduler is not available”断言。两个快照保留相同原始故障，不应把该次生断言当作首次异常。

这里也再次暴露架构故障报告的边界：目前直接进入内核致命报告的检查只覆盖地址零，`0x10` 仍会进入需要可调度上下文的修复器。这是诊断路径的不足，不能解释或修复 `lwp->aspace` 为零的来源。该现象与此前 LWP 被提前回收的故障类别一致，但本轮未捕获该对象的第一次释放或重复 PID 释放，保留归因边界。公共代码和 LoongArch 实现均未改动，两种四核 Smart 配置仍未通过并行进程稳定性验收。

所有客体和 GDB 均已结束。记录位于仓库外 `/tmp/la-pressure-20261020`：`manifest.json` 保存源码提交、镜像/配置 SHA-256 和 QEMU 版本，`summary.json` 保存六组完成项、失败阶段和逐轮资源采样；每组目录包含 `guest.log`、`status.json` 和 QEMU 命令，两组失败另含 `failure-1-gdb.log`、`failure-2-gdb.log`，软件组另有 `fault-instruction.log`。可复查：

```sh
python3 /tmp/la-pressure-20261020/report.py
```

### 2026-10-20：限定架构范围的页故障处理修复

用户再次明确“仍只允许修改 LoongArch，保留公共代码不变”。本阶段基于 `6f84156306`，完成后提交为 `8c3bce55b0`。运行代码仅修改 `libcpu/loongarch/common/trap.c`，增加 `examples/test/loongarch_page_fault_test.py` 并更新本文；`components/`、`src/`、公共头文件和 BSP 配置保持不变。

针对上轮 `lwp_aspace_switch+28` 访问 `0x10` 后进入缺页修复器、再触发调度断言的现场，内核页故障在以下情况下直接进入现有致命报告：地址低于有效用户映射起点、处于 ISR/调度临界区/无当前线程、选中的地址空间为空。UP 下保存的 IRQ 状态为关闭时也不进入需要调度的修复器；SMP 下仅关闭本地 IRQ、但调度仍可用的线程上下文保留原行为。致命报告前关闭本地 IRQ，不再额外获取全局 CPU 锁。正常缺页、CoW、用户故障退出和返回时的 IRQ 状态保持原策略。

这解决的是架构页故障处理的次生断言和现场遮蔽，**没有修复活动 LWP 被提前释放或 procfs 节点生命周期问题**，不能据此将多核并行压力失败标记为解决。

宿主检查命令：

```sh
python3 examples/test/loongarch_page_fault_test.py
python3 examples/test/loongarch_exception_test.py
python3 examples/test/loongarch_port_state_test.py
```

新增测试提取实际 `do_page_fault()`，在 ASan/UBSan 下通过 Smart/非 Smart、UP/SMP 共 54 个用例，检查地址空间选择、四类可修复访问、不可调度上下文、IRQ 保留、用户退出及原始异常帧。相同检查用于修改前源码时，在低地址应直接报告而仍进入修复器的分支失败。原有 192 个异常分发用例、19 个 MMU 分配/回滚/替换/回收场景及地址空间切换、syscall 状态检查均通过。

重新构建八组 QEMU 配置：Nano、标准版、Smart 软件 refill、Smart 硬件 PTW，各覆盖关闭 SMP 和四核。GCC 15.2.0 构建无告警，QEMU 10.2.0 最多同时运行两个客体。复用完整 `loongarch_qemu_test.py:exercise()`，八组均完成：官方用例分别为 Nano/标准版 UP 19、四核 23，Smart UP 32、四核 36；非对齐专项通过，每组内核压力为两个工作线程各 2,000 次。四核各完成每核 1,000,000 次 SMP 校验和 1,000 轮线程回收。Smart 完成用户态基础回归、128 次顺序 fork/CoW；四核 Smart 另完成 1,000 次 TLB 压力和一轮并行 4×128，资源均恢复基线。本次常规矩阵的进程压力规模小于历史失败规模，不作为长时间稳定性验收。

另外对新旧两版四核软件/硬件镜像进行受控故障对照：GDB 在 `lwp_aspace_switch+28` 的实际加载指令前将基址寄存器 `s0` 置零，再继续执行。旧版两种模式均进入 `rt_aspace_fault_try_fix()` 的调度断言；新版均直接输出 `Kernel page fault`，保存帧确认原始 ERA 为 `lwp_aspace_switch+28`、BADV=`0x10`、PRMD=`0`，没有出现该次生调度断言。注入只修改独立客体寄存器，未改源码；它验证异常处理行为，不是自然故障或原始生命周期问题的修复证明。首版验证脚本未兼容地址输出的前导零而超时，已保留记录；修正匹配后四组对照均通过。

最终镜像另完成约六分钟的两客体并行压力。两种四核 Smart 均通过每核 1,000,000 次 SMP 校验、5,000 轮线程回收、三轮各 10,000 次 TLB 测试、一轮每工作线程 2,000 次内核压力和用户态基础回归；随后各完成三轮 `stress 4096`、六轮 `parallel 4 1024`。每种模式九轮进程压力后的首次资源采样均恢复到堆 78,592 字节、空闲页 31,655/31,669，未使用额外等待才达到基线。此次自然压力不加载跟踪或预设断点，软件/硬件分别耗时 361.68/358.32 秒，均完成线程列表检查并正常结束客体。本轮未复现原始失败；此前软件路径曾在第八轮才失败，且公共组件缺陷未改，因此这六轮通过仍不能作为根因已解决或长期稳定的证明。

本阶段记录位于仓库外 `/tmp/la-fault-context-fix`：`summary.json` 为八组回归，`manifest.json` 为源码/镜像/配置校验值，`current-model-final.log` 和 `baseline-model-final.log` 为最终定向测试的新旧源码对照，`injection-v2-summary.json`、`inject-v2-*/` 为最终故障对照及保存帧；首版脚本记录保留在 `injection-summary.json`、`inject-*/`。`pressure/summary.json` 及各模式目录保存追加压力的全部阶段、资源采样和串口输出。全部 QEMU/GDB 已结束，BSP 配置已恢复。未进行 LS2K 实板、1/2/8/16 核或完整 Smart libc 应用验收。

2026-10-22 交接核对：上述三个最终汇总仍分别记录八组回归、四组受控对照、两组追加压力通过；`manifest.json` 中 `trap.c` 和定向测试的 SHA-256 与当前源码一致。清单的 `base_commit` 为修改前的 `6f84156306`，须同时读取 `changes` 和文件校验值，不能将最新镜像误认作修改前基线。复跑入口保存在同目录的 `run_matrix.py`、`check_switch_fault.py`、`pressure/run_pressure.py`；使用前检查其中固定的输出目录、ELF 和配置路径，为新实验指定独立目录，不覆盖既有失败或通过记录。本次只核对记录，没有重跑这些脚本。

## 9. 后续 Codex 的工作约定

- 用户最新任务决定本轮改动范围；本文是交接资料，不是要求每次自动展开所有待办。
- 先用 `rg` / `rg --files` 定位相关代码，用 `git diff` 了解已有修改，不覆盖他人工作。独立读取可以并行；同一 BSP 的配置、构建和测试必须串行。
- 在具备 `functions.exec` / `exec_command` 的会话中通过这些工具运行命令；长测试写日志到仓库外并取得会话 ID，后续轮询检查退出码和末尾标记。工具名以当次会话为准，不依赖旧会话 ID。
- 用户明确要求只由一个 Agent 工作，不启动子代理。已有阶段性提交要求，可在完成相关验证后提交本阶段文件；不要顺带推送、重写历史或夹带无关修改。
- 源码遵循 [中文编码规范](../documentation/7.contribution/coding_style_cn.md) / [英文编码规范](../documentation/7.contribution/coding_style_en.md)。核心接口使用英文 Doxygen `@brief`、`@param`、`@return`，必要时用 `@note` 说明锁、IRQ、地址或调用上下文约束，避免解释显而易见的语句。
- 修改必须同时遵守目录职责和当前授权边界：外设在 BSP common，共用 CPU 机制在 libcpu，芯片差异在板级资源接口，测试放 `examples/test`。当前公共代码只读；确认属于内核/组件的问题应保留证据及修复建议，不能自行修改，也不能复制到架构目录绕过限制。
- 改动后先运行针对性检查；涉及公共 CPU/ABI/SMP 行为时覆盖 UP/SMP、Nano/Smart、软件 refill/硬件 PTW，必要时加标准版。网络改动运行模型及两款 BSP 矩阵。若工具或硬件缺失，明确写出未执行部分。
- 提交前检查 `git diff --check` 和暂存 diff，只暂存本任务文件。不得提交日志、ELF、磁盘镜像、下载手册、本地环境或工具链。测试生成的配置差异需核对，不用 `git reset --hard` 清理。
- 完成一个阶段后记录提交、实际测试命令/结果、尚未解决问题，并更新本文或已有 BSP README；不能只写“测试完全”或“全部通过”而省略范围。

进一步阅读：[QEMU BSP README](../bsp/qemu-virt64-loongarch/README.md)、[LS2K1000LA / LS2K0300 README](../bsp/loongarch/ls2k1000/README.md)。
