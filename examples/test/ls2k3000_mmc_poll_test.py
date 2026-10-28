#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-28     LoongsonLab               retain native MMC regression runner
#
# Description:
#   Compile the production board engine with an MMIO fault model and sanitizers.
#   This checks software ownership rules; it does not emulate MMC silicon.

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    output = args.output or Path(tempfile.mkdtemp(prefix='ls2k3000-mmc-'))
    output.mkdir(parents=True, exist_ok=True)
    driver = ROOT / 'bsp/loongarch/ls2k3000/drivers/sd'
    binary = output.resolve() / 'mmc-poll-test'
    subprocess.run([
        args.cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I', str(driver), str(Path(__file__).with_suffix('.c')),
        str(driver / 'mmc_poll.c'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
