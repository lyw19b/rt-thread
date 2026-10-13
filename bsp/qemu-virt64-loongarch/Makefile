
GNU_GDB := /home/airxs/user/github/toolchains/loongarch-gnu-toolchain/loongarch64-lp64d-linux-gnu


VERBOSE :=
V ?=

ifeq ($(V),1)
VERBOSE := --verbose
else
VERBOSE :=
endif

all:
	scons -j 6 $(VERBOSE)
	loongarch64-linux-gnu-objdump -ald rtthread.elf > rtthread.elf.S

run:
	./run.sh


debug:
	./qemu-dbg.sh &
	sleep 1
	$(GNU_GDB)/bin/loongarch64-unknown-linux-gnu-gdb rtthread.elf \
	  -ex 'target remote localhost:1234' \
	  -ex 'b debug_trace' \
	  -ex 'continue' \
	  -ex 'disp /16i $$pc'


menuconfig:
	scons --menuconfig

.PHONY: clean
clean:
	rm -rf build __pycache__ loongarch64_virt.* rtthread.* .sconsign.dblite \
	rtthread.elf.S
