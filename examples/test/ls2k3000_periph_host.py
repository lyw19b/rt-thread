#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
# SPDX-License-Identifier: Apache-2.0
#
# File: ls2k3000_periph_host.py
# Author: LoongsonLab

# Description:
#   Compile the production register engines with a fault-injecting MMIO model.

from pathlib import Path
import subprocess
import tempfile


def main():
    tests = Path(__file__).resolve().parent
    drivers = tests.parents[1] / 'bsp/loongarch/ls2k3000/drivers'
    sources = ['i2c/i2c_poll.c', 'spi/spi_poll.c', 'rtc/rtc_toy.c',
               'pwm/pwm_hw.c', 'power/watchdog_hw.c', 'timer/hpet_counter.c']
    with tempfile.TemporaryDirectory(prefix='ls2k3000-periph-host-') as directory:
        binary = Path(directory) / 'test'
        command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   '-fno-pie', '-no-pie']
        command += ['-I' + str(drivers / name) for name in
                    ('boards', 'i2c', 'spi', 'rtc', 'pwm', 'power', 'timer')]
        command += [str(tests / 'ls2k3000_periph_host.c')]
        command += [str(drivers / source) for source in sources]
        subprocess.run(command + ['-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
