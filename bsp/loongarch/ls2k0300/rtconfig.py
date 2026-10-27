# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-27     LoongsonLab               integrate isolated LS2K0300 BSP

# Description:
#   LS2K0300 development BSP: rtconfig.

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'common'))
from toolchain import *

POST_ACTION = image_actions('0x9000000000200000', 'rtthread-2k0300')
