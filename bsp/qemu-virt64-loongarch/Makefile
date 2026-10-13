SCONS ?= scons
GDB ?= loongarch64-linux-musl-gdb

.PHONY: all run debug menuconfig clean
all:
	$(SCONS) $(SCONSFLAGS)

run:
	./run.sh

debug:
	$(GDB) rtthread.elf -ex 'target remote localhost:1234'

menuconfig:
	$(SCONS) --menuconfig

clean:
	$(SCONS) --clean
