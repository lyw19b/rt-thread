import os
import sys
import rtconfig

from rtconfig import RTT_ROOT

sys.path = sys.path + [os.path.join(RTT_ROOT, 'tools')]
from building import *

TARGET = 'rtthread.' + rtconfig.TARGET_EXT

DefaultEnvironment(tools=[])
env = Environment(tools = ['mingw'],
    AS = rtconfig.AS, ASFLAGS = rtconfig.AFLAGS,
    CC = rtconfig.CC, CFLAGS = rtconfig.CFLAGS,
    CXX = rtconfig.CXX, CXXFLAGS = rtconfig.CXXFLAGS,
    AR = rtconfig.AR, ARFLAGS = '-rc',
    LINK = rtconfig.LINK, LINKFLAGS = rtconfig.LFLAGS)
env.PrependENVPath('PATH', rtconfig.EXEC_PATH)
env['ASCOM'] = env['ASPPCOM']

env.Tool('compilation_db')
cdb = env.CompilationDatabase('compile_commands.json')

Export('RTT_ROOT')
Export('rtconfig')

# prepare building environment
objs = PrepareBuilding(env, RTT_ROOT, has_libcpu = False)

stack_size = 16384

if GetDepend('RT_USING_SMART') and GetDepend('SOC_LS2K1000LA'):
    raise RuntimeError('LS2K1000LA Smart memory setup is not ported; use bsp/qemu-virt64-loongarch')

if GetDepend('__STACKSIZE__'):
    stack_size = GetDepend('__STACKSIZE__')
with open('link_stacksize.lds', 'w') as stack_lds:
    stack_lds.write('__STACKSIZE__ = %d;\n' % stack_size)

if GetDepend('SOC_LS2K0300'):
    paged = bool(GetDepend('KMD_USE_PAGE'))
    virtual_base = 0xffffffe000000000 if paged else 0x9000000000000000
    with open('link_kmd.lds', 'w') as mapping_lds:
        mapping_lds.write('__kernel_virtual_base__ = %#x;\n' % virtual_base)
        mapping_lds.write('__kernel_load_offset__ = %#x;\n' % (virtual_base if paged else 0))
        mapping_lds.write('__kernel_entry = _start - __kernel_load_offset__;\n')
    Depends(TARGET, 'link_kmd.lds')
    if paged:
        rtconfig.POST_ACTION = rtconfig.image_actions('0x200000', 'rtthread-2k0300-page')

# make a building
Depends(TARGET, ['kernel.specs', '../common/kernel.specs', '../common/link.lds'])
DoBuilding(TARGET, objs)
