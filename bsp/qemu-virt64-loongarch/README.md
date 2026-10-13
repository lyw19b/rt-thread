# QEMU LoongArch64

支持单核 LoongArch64 的 Nano、标准版和 Smart。默认使用 LP64D ABI，保存浮点寄存器；不启用 LSX/LASX。共享移植代码位于 `libcpu/loongarch/common`。

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

## QEMU

运行脚本使用 256 MiB RAM、单核 `virt` 机型。可设置 `QEMU` 选择可执行文件，`KERNEL` 选择内核。`qemu-dbg.sh` 会等待 GDB 连接端口 1234。

标准版和 Smart 使用 RAM 磁盘。`run.sh` 首次运行会创建 64 MiB FAT 镜像 `sd.img`；也可通过 `QEMU_DISK` 指定已有镜像。QEMU generic loader 将镜像加载到物理地址 `0x0c000000`，该区域从页分配器中保留。客体的写操作仅修改内存，退出 QEMU 后不会写回镜像。

源 BSP 的 VirtIO MMIO 驱动作为可选配置保留，要求 QEMU 提供物理地址 `0x30001000` 起的六个 MMIO 设备和 IRQ 16～21。主线 QEMU 的 LoongArch `virt` 不提供这些总线，默认配置因此关闭此接口。

## Smart 用户程序

用户程序使用 RT-Thread Smart 系统调用编号，参数放在 `a0`～`a6`，编号放在 `a7`，由 `syscall 0` 陷入内核。普通 Linux musl 用户程序的系统调用编号不同，不能直接作为 Smart 应用运行。内核的 `kernel.specs` 将 musl 分配函数绑定到 RT-Thread 堆，内核态 stdio 的调用由架构层转换。

## 回归与压力测试

在仓库根目录运行：

```sh
python3 examples/test/loongarch_qemu_test.py --mode all --parallel \
    --iterations 20000 --fork-loops 1024 --repeats 3
```

需要 SCons、Python `kconfiglib`/`pexpect`、`mkfs.fat` 和 mtools 的 `mcopy`。脚本优先使用 `RTT_EXEC_PATH`，未设置时会查找 `~/local/*/bin/loongarch64-linux-musl-gcc`；`QEMU`、`MCOPY` 可指定工具路径。用 `--mode nano` 或 `--mode smart` 单独测试，`--output /tmp/loongarch-test` 指定日志位置。构建会临时替换 BSP 配置，退出时恢复原 `.config` 并重新生成 `rtconfig.h`；不要同时在同一 BSP 目录手动构建。

脚本会编译并在 QEMU 中执行：

- Nano、标准版、Smart：官方内核测试（线程、栈溢出检测、调度、IPC、原子操作、堆、定时器），其中定时器压力测试持续 180 秒。
- 两个工作线程反复交换消息、分配并校验内存，检查软定时器、抢占和浮点上下文，每轮结束核对堆用量。
- Smart：官方 MM/LWP 映射测试；用户态文件读写、非法指针、信号屏蔽/恢复、浮点寄存器及控制状态、clone/TLS、RT IPC、用户线程正常返回、fork/CoW/wait、exec 和进程异常隔离。
- Smart：重复执行 fork/CoW 和进程创建/退出，核对每轮后的堆用量与空闲页数。每批 32 次 fork 暂停 20 ms，让低优先级 `tsystem` 完成延迟回收；持续占满 CPU 会使该线程饥饿，不能把 `waitpid` 返回视为内存已经回收。

`loongarch_user_test.c` 是使用官方 Smart ABI 的独立测试程序，用现有 Linux musl 工具链的头文件和汇编器构建，不链接 Linux 用户态 libc。它验证内核 ABI，不能替代对完整 Smart musl、动态链接器和第三方应用的测试。本 BSP 当前验证范围为单核、LP64D、4 KiB 页和 RAM FAT 根文件系统；LSX/LASX、SMP、主线 QEMU VirtIO PCI 网络/磁盘不在此范围内。
