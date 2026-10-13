import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'common'))
from toolchain import *

POST_ACTION = image_actions('0x9000000098000000', 'rtthread-2k1000la')
