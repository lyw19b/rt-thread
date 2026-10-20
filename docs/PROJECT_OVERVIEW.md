# RT-Thread LoongArch 项目交接

更新时间：2026-10-20。本文面向下一次在本仓库工作的 Codex 和维护者，记录项目状态、代码入口、构建测试方法及尚未完成的验证。后续修改代码时，应同步更新相关状态，不能把历史测试结果当作新提交的验证结果。

## 1. 下次启动先看这里

- 当前仓库：`/home/airxs/user/aicode/kernel/rt-thread`。
- 当前分支：`rt-loong64-v0.1`。最新 CPU 重构代码提交为 `1e43f960c9`；本次验证范围见第 8 节。启动新任务时仍应重新检查提交和工作区状态。
- 参考仓库：`/home/airxs/user/aicode/kernel/rt-thread-2k1000`。仅用于核对硬件行为和移植差异，不直接照搬其驱动结构或代码风格。
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

## 2. 当前支持范围与未完成事项

| 目标 | 配置与默认核数 | Smart / 页表遍历 | 验证边界 |
| --- | --- | --- | --- |
| QEMU LoongArch `virt` | Nano、标准版、Smart；默认 SMP 4 核，可关闭 SMP，配置范围 1～16 核 | 软件 refill、硬件 PTW | 已有回归及压力记录；16 核组合长压仍有未关闭问题 |
| LS2K1000LA | 标准版、网络版；默认 SMP 2 核，可改 1 核或关闭 SMP | **板级 Smart 内存初始化未移植**；芯片只允许软件 refill | 交叉编译、宿主驱动模型通过；未实板启动或联网验收 |
| LS2K0300 | Nano、标准版、网络版、Smart；默认开启 SMP 框架，限定 1 核 | 软件 refill；Smart 使用临时根文件系统 | 交叉编译、宿主驱动模型通过；未实板验收 |

本项目的 `ls2k1000` 指 LoongArch 版 **2K1000LA**，不要与仓库内其他 MIPS 龙芯 BSP 混淆。2K1000 当前没有完整 Nano/Smart defconfig 集合；不要把 QEMU 或 2K0300 的能力写成该板已经具备。

后续需要继续完成：

1. **16 核 Smart 软件路径组合压力停滞。** 历史上发生过输出停滞，尚未完成长时间稳定性验收。`c5a8816f3e` 修复了 TLB 测试自身的启动屏障问题，但不能据此判定所有停滞都已解决。应重新保存完整复现日志、ELF、配置及多核栈，再判断是否还有内核问题。
2. **LS2K1000LA Smart 板级内存初始化。** `bsp/loongarch/common/build.py` 当前显式拒绝此组合。需要完成内存布局、保留区和地址空间初始化，不能仅删除报错来“支持”Smart。
3. **完整 Smart 用户态环境。** 当前独立 ABI 测试程序不链接 Linux 用户态 libc；尚不能代替 Smart musl、动态链接器及真实应用的验证。
4. **实板测试。** 两款芯片的固件交接、串口时钟、次核启动、PHY、RGMII 延迟、DMA、双网口及长压均需真实硬件。QEMU `virt` 不模拟这些板载外设。
5. 2K0300 尚无 SDIO/eMMC 存储驱动及板级复位/关机实现。2K1000 的可选 AHCI 沿用此前迁入版本，不属于已完成的网络驱动重构范围。

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
- 寄存器定义：`include/loongarch_regs.h` 保存 C/汇编共用的硬件编码，`include/loongarch.h` 提供 C 访问接口。硬件数值、原有宏名和栈偏移保持兼容；`regdef.h`、`fpregdef.h` 按用户要求保留。
- 非对齐模拟：`unaligned.c` 按表解码 28 种标量访存指令，`include/inst.h` 仅声明访问描述及模拟接口，`unaligned_gcc.S` 按字节读写。当前 `do_ale()` 仍执行致命异常策略，未接入模拟器；模拟器直接访问 live FPR，不提供访存异常修复，不能直接改接到现有异常返回路径。
- 异常与页故障：`exception_gcc.S`、`trap.c`；页表操作在 `mmu.c`，页表遍历初始化在 `setup.c`。当前为 LP64D、4 KiB 页、三级页表，禁用 LSX/LASX。
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

2026-10-20 已在本机核实：

| 工具 | 路径或状态 |
| --- | --- |
| LoongArch musl GCC | `~/local/loongarch64-linux-musl-gcc-15.2.0/bin/loongarch64-linux-musl-gcc`，15.2.0 |
| LoongArch GDB | 同一 `bin` 下 `loongarch64-linux-musl-gdb`，16.3 |
| QEMU，支持硬件 PTW | `~/local/qemu-la64/bin/qemu-system-loongarch64`，10.2.0 |
| 另一套 QEMU | `~/local/qemu-loongarch64-10.1.2/bin/qemu-system-loongarch64`；不要用它验证硬件 PTW |
| SCons / Python / 宿主 C 编译器 | `/usr/bin/scons`、`/usr/bin/python3`、`/usr/bin/cc` |
| Python 模块 | `kconfiglib`、`pexpect` 可导入 |
| FAT 工具 | `/usr/sbin/mkfs.fat` 存在；**`mcopy` 当前不在 PATH 中，`~/local` 和 `/tmp` 下也未找到** |

建议在执行本页命令的 shell 中设置：

```sh
export RTT_EXEC_PATH="$HOME/local/loongarch64-linux-musl-gcc-15.2.0/bin"
export RTT_CC_PREFIX=loongarch64-linux-musl-
export QEMU="$HOME/local/qemu-la64/bin/qemu-system-loongarch64"
export PATH="$RTT_EXEC_PATH:/usr/sbin:$PATH"
```

工具链可以用 `rg --files "$HOME/local" -g '*musl-gcc' -g '*gdb' -g 'qemu-system-loongarch64'` 定位。自动测试优先用 `RTT_EXEC_PATH`；未设置时扫描 `~/local/*/bin/loongarch64-linux-musl-gcc`，因此多套工具链并存时建议显式设置。

标准版/Smart 自动回归还需要 mtools 的 `mcopy`。在 Debian/Ubuntu 环境可按本机管理方式安装 `mtools`；若已有自定义安装，可设置 `MCOPY=/实际路径/mcopy` 或传 `--mcopy`。不要把工具二进制加入仓库。交接文档初稿未重新运行回归；本次 CPU 重构验证将 mtools 4.0.43 临时解包到 `/tmp/la-linux-refactor/tools`，使用其中的 `usr/bin/mcopy`，未安装到系统 PATH。后续会话应重新确认该临时路径是否存在。

```sh
command -v scons python3 cc mkfs.fat mcopy
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

# 四核完整回归及压力；先补齐 mcopy。
python3 examples/test/loongarch_qemu_test.py --mode all --parallel --cpus 4 \
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

脚本先替换 `.config`，启用自测及官方 utest，构建 ELF，必要时构建用户测试程序和 FAT 镜像，然后检查：

- 串口输入序号、`port_test`、官方内核 utest；官方定时器压力本身包含 180 秒测试，不要仅因短期无输出就判为挂死。
- `port_unaligned_test` 直接调用模拟器，覆盖 28 种指令、32 个目标寄存器、正负值及非对齐偏移，共 12,880 个数据访问用例；另检查拒绝的指令、零长度操作及 C/汇编帧布局。这些检查不依赖 CPU 是否原生支持非对齐访问，也不表示已启用 ALE 软件修复。
- SMP 每核计数、原子操作、自旋锁、IPI、tick、线程迁移、FPU、线程退出及远端删除回收。
- Smart 官方 MM/LWP 测试、实际 PTW 状态、用户读写及非法指针、信号、浮点状态、TLS/clone、RT IPC、fork/CoW/wait/exec、进程异常隔离。
- Smart SMP 关中断的 TLB 映射切换及远端失效，顺序和并行 fork/CoW/exec 压力；每轮比较堆用量与空闲页数。

当前脚本期望官方测试计数：Nano/标准版 UP 为 19，SMP 为 23；Smart UP 为 32，SMP 为 36。上游测试集合变化时要核对期望数量及实际输出，不能简单减少数量或删除失败匹配。

输出目录含 `<mode>-build.log`、`<mode>-qemu.log`、`<mode>.elf`，非 Nano 还含 `fat.img`、`user.elf`、`user-build.log`。脚本在 `finally` 恢复原 `.config` 并生成 `rtconfig.h`，记录 `restore-config.log`；**不会按恢复后的配置重建 BSP 内的 `rtthread.elf`**。测试后直接运行 `run.sh` 前应重新执行 `scons -j8`，或者使用输出目录中已知配置的 ELF。强制杀进程可能绕过恢复，应检查配置差异。

### LS2K 宿主模型和交叉构建

```sh
python3 examples/test/ls2k_platform_test.py
python3 examples/test/ls2k_gmac_test.py --build --bsp ls2k1000 \
    --output /tmp/ls2k1000-check
python3 examples/test/ls2k_gmac_test.py --build --bsp ls2k0300 \
    --output /tmp/ls2k0300-check
```

`ls2k_platform_test.py` 验证两种芯片的 UART、时钟、中断路由/确认/屏蔽和 DMA 通路，临时目录自动删除。GMAC 脚本默认先跑 ASan/UBSan 宿主模型，验证 MDIO 超时、PHY、环回绕、OWN 顺序、坏帧和 lwIP 通知失败重试，两个端口模型各 100,000 帧；`--build` 再跑所选 BSP 的构建矩阵。仅测模型时省略 `--build`。

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

## 9. 后续 Codex 的工作约定

- 用户最新任务决定本轮改动范围；本文是交接资料，不是要求每次自动展开所有待办。
- 先用 `rg` / `rg --files` 定位相关代码，用 `git diff` 了解已有修改，不覆盖他人工作。独立读取可以并行；同一 BSP 的配置、构建和测试必须串行。
- 在具备 `functions.exec` / `exec_command` 的会话中通过这些工具运行命令；长测试写日志到仓库外并取得会话 ID，后续轮询检查退出码和末尾标记。工具名以当次会话为准，不依赖旧会话 ID。
- 没有额外授权时不启动子代理。已有阶段性提交要求，可在完成相关验证后提交本阶段文件；不要顺带推送、重写历史或夹带无关修改。
- 源码遵循 [中文编码规范](../documentation/7.contribution/coding_style_cn.md) / [英文编码规范](../documentation/7.contribution/coding_style_en.md)。核心接口使用英文 Doxygen `@brief`、`@param`、`@return`，必要时用 `@note` 说明锁、IRQ、地址或调用上下文约束，避免解释显而易见的语句。
- 通用问题优先在内核/组件正确层修复；外设在 BSP common，共用 CPU 机制在 libcpu，芯片差异在板级资源接口，测试放 `examples/test`。不要复制第二套驱动。
- 改动后先运行针对性检查；涉及公共 CPU/ABI/SMP 行为时覆盖 UP/SMP、Nano/Smart、软件 refill/硬件 PTW，必要时加标准版。网络改动运行模型及两款 BSP 矩阵。若工具或硬件缺失，明确写出未执行部分。
- 提交前检查 `git diff --check` 和暂存 diff，只暂存本任务文件。不得提交日志、ELF、磁盘镜像、下载手册、本地环境或工具链。测试生成的配置差异需核对，不用 `git reset --hard` 清理。
- 完成一个阶段后记录提交、实际测试命令/结果、尚未解决问题，并更新本文或已有 BSP README；不能只写“测试完全”或“全部通过”而省略范围。

进一步阅读：[QEMU BSP README](../bsp/qemu-virt64-loongarch/README.md)、[LS2K1000LA / LS2K0300 README](../bsp/loongarch/ls2k1000/README.md)。
