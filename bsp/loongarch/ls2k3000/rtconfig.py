# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
# SPDX-License-Identifier: Apache-2.0
#
# File: rtconfig.py
# Author: LoongsonLab
#
# Description:
#   Use the common scalar LoongArch toolchain for EFI payloads.

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'common'))
from toolchain import *

POST_ACTION = OBJCPY + ' -O binary $TARGET rtthread.bin\n' + SIZE + ' $TARGET\n'
