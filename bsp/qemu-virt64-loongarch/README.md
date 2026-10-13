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
