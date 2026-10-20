# QEMU LoongArch64

支持 LoongArch64 的 Nano、标准版和 Smart，均可启用 SMP。三个 defconfig 默认开启 SMP、配置 4 核；menuconfig 中可选 1～16 核，也可关闭 SMP。默认使用 LP64D ABI，保存浮点寄存器；不启用 LSX/LASX。共享移植代码位于 `libcpu/loongarch/common`。

## 构建

使用 LoongArch64 musl GCC，将工具链 `bin` 目录设置为 `RTT_EXEC_PATH`，可用 `RTT_CC_PREFIX` 更改默认的 `loongarch64-linux-musl-` 前缀。

```sh
export RTT_EXEC_PATH="$HOME/local/loongarch64-linux-musl-gcc-15.2.0/bin"
cp nano_defconfig .config  # 或 standard_defconfig、smart_defconfig
scons --pyconfig-silent
scons -j8
./run.sh
```

配置头文件由 SCons 生成。Nano 可以运行 `port_test`，测试线程调度、同步通信、软定时器、内存分配和浮点上下文。

启用 `BSP_USING_SELFTEST` 后还可运行 `port_unaligned_test`，直接测试 28 种标量访存指令的模拟、全部 32 个目标寄存器及异常帧布局；Nano、标准版和 Smart 均可运行。当前 ALE 异常仍按致命异常处理，这个测试不启用自动非对齐修复。

## QEMU

运行脚本使用 256 MiB RAM、`virt` 机型，从生成的 `rtconfig.h` 读取核数并传给 QEMU `-smp`。可设置 `QEMU` 选择可执行文件，`KERNEL` 选择内核。`qemu-dbg.sh` 会等待 GDB 连接端口 1234。

标准版和 Smart 使用 RAM 磁盘。`run.sh` 首次运行会创建 64 MiB FAT 镜像 `sd.img`；也可通过 `QEMU_DISK` 指定已有镜像。QEMU generic loader 将镜像加载到物理地址 `0x0c000000`，该区域从页分配器中保留。客体的写操作仅修改内存，退出 QEMU 后不会写回镜像。

源 BSP 的 VirtIO MMIO 驱动作为可选配置保留，要求 QEMU 提供物理地址 `0x30001000` 起的六个 MMIO 设备和 IRQ 16～21。主线 QEMU 的 LoongArch `virt` 不提供这些总线，默认配置因此关闭此接口。

## Smart 页表遍历

`Smart TLB refill` 提供软件 refill 和硬件 PTW 二选一，默认使用软件 refill。两条路径共用三级页表和 TLB shootdown；每核独立设置 PWCL/PWCH。软件模式明确清除 PTW 使能位，硬件模式检查 CPUCFG2.PTW。2K0300、2K1000LA 不开放硬件 PTW 选项。

硬件 PTW 测试需要支持该功能的 QEMU（本次使用 10.2.0）。`run.sh` 检测到硬件配置时自动使用 `-cpu la464,ptw=on`，也可用 `QEMU_CPU` 指定模型。回归脚本用 `--ptw hardware` 构建并运行硬件路径；软件路径使用默认的 `--ptw software`。`port_mmu` 检查当前核实际使能状态，`smp_test` 的 IPI 回调核对所有在线核心。

```sh
QEMU="$HOME/local/qemu-la64/bin/qemu-system-loongarch64" \
python3 examples/test/loongarch_qemu_test.py --mode smart --cpus 4 --ptw hardware
```

## Smart 用户程序

用户程序使用 RT-Thread Smart 系统调用编号，参数放在 `a0`～`a6`，编号放在 `a7`，由 `syscall 0` 陷入内核。普通 Linux musl 用户程序的系统调用编号不同，不能直接作为 Smart 应用运行。内核的 `kernel.specs` 将 musl 分配函数绑定到 RT-Thread 堆，内核态 stdio 的调用由架构层转换。

## 回归与压力测试

在仓库根目录运行：

```sh
python3 examples/test/loongarch_qemu_test.py --mode all --parallel --cpus 4 --smp-iterations 1000000 \
    --iterations 20000 --fork-loops 1024 --repeats 3
```

需要 SCons、Python `kconfiglib`/`pexpect`、`mkfs.fat` 和 mtools 的 `mcopy`。脚本优先使用 `RTT_EXEC_PATH`，未设置时会查找 `~/local/*/bin/loongarch64-linux-musl-gcc`；`QEMU`、`MCOPY` 可指定工具路径。`--cpus 0` 测试关闭 SMP 的配置，`--cpus 1` 测试单核 SMP；可用 2、4、8、16 测试更多核心。用 `--mode nano` 或 `--mode smart` 单独测试，`--output /tmp/loongarch-test` 指定日志位置。构建会临时替换 BSP 配置，退出时恢复原 `.config` 并重新生成 `rtconfig.h`；不要同时在同一 BSP 目录手动构建。

脚本会编译并在 QEMU 中执行：

- Nano、标准版、Smart：官方内核测试（线程、栈溢出检测、调度、IPC、原子操作、堆、定时器），其中定时器压力测试持续 180 秒。
- 非对齐模拟：12,880 个数据访问用例，另检查不支持的指令、零长度操作及 C/汇编帧布局；确认符号扩展、浮点位模式、ERA 推进和未涉及的内存/帧字段。
- SMP：每核绑定一个工作线程，校验自旋锁、原子操作、每核 tick、跨核迁移、IPI 回调和浮点上下文；反复测试线程自然退出与远端删除后的栈回收。
- 串口：逐条发送含序号的命令，校验字符顺序。
- 两个工作线程反复交换消息、分配并校验内存，检查软定时器、抢占和浮点上下文，每轮结束核对堆用量。
- Smart：官方 MM/LWP 映射测试；用户态文件读写、非法指针、信号屏蔽/恢复、浮点寄存器及控制状态、clone/TLS、RT IPC、用户线程正常返回、fork/CoW/wait、exec 和进程异常隔离。
- Smart SMP：交替由单核及所有核心修改映射，在关中断且不切换上下文的条件下核对远端 TLB 失效。
- Smart：重复执行 fork/CoW 和进程创建/退出，核对每轮后的堆用量与空闲页数。每批 32 次 fork 暂停 20 ms，让低优先级 `tsystem` 完成延迟回收；持续占满 CPU 会使该线程饥饿，不能把 `waitpid` 返回视为内存已经回收。SMP 另运行每核一个进程的并行 fork/CoW/exec，每批所有进程合计最多执行 32 次 fork，然后退出工作进程并暂停 100 ms；等待指定子进程时仅对 `EINTR` 重试，仍检查 PID 和退出状态。测试构建按并行规模增加 PID 槽位，避免将配置的进程数量上限误作移植失败。

`loongarch_user_test.c` 是使用官方 Smart ABI 的独立测试程序，用现有 Linux musl 工具链的头文件和汇编器构建，不链接 Linux 用户态 libc。它验证内核 ABI，不能替代对完整 Smart musl、动态链接器和第三方应用的测试。本 BSP 验证 LP64D、4 KiB 页和 RAM FAT 根文件系统；LSX/LASX、主线 QEMU VirtIO PCI 网络/磁盘不在此范围内。

SMP 启动与 LS2K 共用 IOCSR 邮箱和 IPI，邮箱 0 写入次核入口物理地址。各核独立初始化异常、时钟和内核栈，设备中断仍交给启动核。Smart 页表修改同步等待在线核确认 TLB 失效；关中断等待自旋锁时也处理失效请求，避免页表锁与 IPI 互相等待。不支持 CPU 热插拔。

## 验证记录

2026-10-14，使用 LoongArch64 musl GCC 15.2.0、QEMU 10.1.2 / 10.2.0；硬件 PTW 使用 10.2.0。

| 配置 | 关闭 SMP 的官方测试 | 四核 SMP 的官方测试 | 内核压力测试 |
| --- | --- | --- | --- |
| Nano | 19/19 | 23/23 | 两种配置各 3 轮，每线程 20,000 次 |
| 标准版 | 19/19 | 23/23 | 两种配置各 3 轮，每线程 20,000 次 |
| Smart 软件 refill | 32/32 | 36/36 | 两种配置各 3 轮，每线程 20,000 次 |
| Smart 硬件 PTW | 32/32 | 36/36 | 两种配置各 3 轮，每线程 20,000 次 |

上述官方测试包含 180 秒定时器压力测试。Smart 两种遍历模式在关闭 SMP 及四核配置下，均通过三轮各 1,024 次顺序 fork/CoW；四核另通过三轮、每轮四个进程各 1,024 次并行 fork/CoW/exec。每轮结束检查堆和空闲页数，均与该次启动的基线一致。

补充 SMP 测试覆盖 1、2、8、16 核：Nano 和标准版每核执行 1,000,000 次计数/浮点校验，并执行 1,000 轮线程退出与远端删除；Smart 软件路径每核执行 1,000,000 次校验及 10,000 轮 TLB 映射切换。硬件 PTW 四核也通过 10,000 轮 TLB 测试，其中单核修改映射、其余核心只读的轮次用于验证远端失效；所有核心均关闭中断，不借助调度清除旧映射。

Smart 软件路径八核另通过三轮、每轮八个进程各 1,024 次并行 fork/CoW/exec，资源计数保持一致。十六核在一轮组合压力中出现输出停滞，原因仍在定位，暂不标记为通过长时间稳定性验收。

这些记录对应上文列出的测试范围，不包含完整 Smart libc、动态链接应用或 LS2K 实板运行。

2026-10-20，CPU 重构提交 `1e43f960c9` 使用 GCC 15.2.0、QEMU 10.2.0 重新通过上述表格的八组 UP/四核官方测试及每组三轮内核压力；四组 Smart 的顺序压力和四核两组的并行压力均通过，资源基线一致。新增非对齐模拟专项测试每组通过 12,880 个数据用例，并检查拒绝指令、零长度操作及帧布局。四核 SMP 校验每核 1,000,000 次，线程回收 1,000 轮；Smart 四核 TLB 测试为每种模式 3×1,000 轮。本轮未重测 1/2/8/16 核，十六核待定位问题保持开放。详细命令、差分检查及日志位置见 [项目交接第 8 节](../../docs/PROJECT_OVERVIEW.md#8-历史验证与阶段提交)。
