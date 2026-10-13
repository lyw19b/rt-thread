import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'common'))
from toolchain import *

POST_ACTION = image_actions('0x9000000000200000', 'rtthread-2k0300')
