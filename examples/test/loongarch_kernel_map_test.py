#!/usr/bin/env python3
# Copyright (c) 2026, RT-Thread Development Team
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author       Notes
# 2026-10-24     OpenLANPU    Retain independent kernel mapping stress builds
"""Build private Novae Smart mapping variants and retain stress/fault evidence."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

import pexpect

KERNEL = Path(__file__).resolve().parents[2]
PROJECT = KERNEL.parents[2]
SUITE = PROJECT / 'system/docs/rthread/bugs/tests'
sys.path.insert(0, str(PROJECT / 'system/scripts'))
sys.path.insert(0, str(PROJECT / 'system/display/novauiOS/tools'))
from rtthread import build_environment
from rtos_image import build_smart_kernel


def config_set(config, name, value):
    config = re.sub(rf'^(?:CONFIG_{name}=.*|# CONFIG_{name} is not set)\n', '', config, flags=re.M)
    return config + f'CONFIG_{name}={value}\n'


def build(args):
    env = build_environment(args.toolchain, args.prefix)
    kernel = build_smart_kernel(args.output, env, args.jobs, args.application,
                                kernel_mapping=args.mode)
    bsp = kernel.parent
    if args.heap_end_mib != 96:
        path = bsp / 'drivers/board.h'
        source = path.read_text()
        if '0x06000000UL' not in source:
            raise ValueError('Expected the large novauiOS image heap boundary')
        path.write_text(source.replace('0x06000000UL', f'0x{args.heap_end_mib << 20:08x}UL'))
    # Four cores exist only in the separate SMP test machine. Real LS2K0300
    # remains limited to one CPU; never relax the production BSP capability.
    if args.cpus == 4:
        path = bsp / 'Kconfig'
        path.write_text(path.read_text().replace('range 1 1 if RT_USING_SMP',
                                                'range 1 4 if RT_USING_SMP'))
    path = bsp / '.config'
    config = path.read_text()
    for name, value in {'RT_USING_SMP': 'y' if args.cpus > 1 else 'n', 'RT_CPUS_NR': args.cpus,
                        'RT_LWP_MAX_NR': 48, 'BSP_USING_SELFTEST': 'y',
                        'RT_USING_DFS_PROCFS': 'y', 'RT_USING_UTEST': 'y',
                        'RT_USING_UTESTCASES': 'y', 'RT_UTEST_MM_API': 'y',
                        'RT_UTEST_MM_LWP': 'y'}.items():
        config = config_set(config, name, value)
    path.write_text(config)
    # The UI fixture has a read-only ROMFS root and a writable tmpfs at /mnt.
    # Keep the upstream mmap assertions intact, changing only the test filename.
    for name in ['lwp_mmap_fd.c', 'lwp_mmap_fd_map_fixed_merge.c', 'lwp_mmap_fd_map_fixed_split.c']:
        path = bsp.parents[2] / 'components/mm/utest' / name
        path.write_text(path.read_text().replace('"/test_mmap"', '"/mnt/test_mmap"'))
    with (args.output / 'suite-build.log').open('w') as log:
        subprocess.run(['scons', '--pyconfig-silent'], cwd=bsp, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        for source, filename, audit in [('smart_extended_user.c', 'smart-x.elf', False),
                                         ('loongarch_user_test.c', 'user.elf', True)]:
            original = KERNEL / 'examples/test' / source if audit else SUITE / source
            temporary = args.output / source
            temporary.write_text(original.read_text().replace('"/roundtrip"', '"/mnt/roundtrip"')
                                  .replace('"/user.elf"', '"/mnt/user.elf"'))
            elf = args.output / filename
            subprocess.run([str(args.toolchain / (args.prefix + 'gcc')), '-nostdlib', '-static',
                            '-no-pie', '-fno-pie', '-fno-builtin', '-mno-lsx', '-mno-lasx',
                            '-O2', '-g', '-Wall', '-Wextra', '-Wl,-e,_start,-Ttext=0x200000000',
                            '-I' + str(KERNEL / 'examples/test'), str(temporary),
                            str(KERNEL / 'examples/test/loongarch_user_entry.S'), '-o', str(elf)],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            command = [sys.executable, str(SUITE / 'embed_smart_probe.py'), str(elf),
                       str(bsp / 'applications' / ('audit_user.c' if audit else 'smart_install.c'))]
            if audit:
                command.append('--audit')
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
        for name in ['smart_memory_probe.c', 'public_fix_probe.c', 'procfd_race_probe.c']:
            shutil.copy2(SUITE / name, bsp / 'applications' / name)
        subprocess.run(['scons', f'-j{args.jobs}'], cwd=bsp, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    for name in ['kernel.log', 'suite-build.log']:
        diagnostics = re.sub(r'\x1b\[[0-9;]*[mK]', '', (args.output / name).read_text())
        for line in diagnostics.splitlines():
            # Existing novaui_page shell export uses RT-Thread's generic cast.
            if 'warning:' in line and '-Wcast-function-type' in line:
                continue
            if 'warning:' in line or 'error:' in line:
                raise RuntimeError('Compiler/linker diagnostic in ' + name + ': ' + line)
    subprocess.run([str(args.toolchain / (args.prefix + 'readelf')), '-l', str(kernel)],
                   stdout=(args.output / 'elf-segments.txt').open('w'), check=True)
    (args.output / 'build.json').write_text(json.dumps({
        'kernel': str(kernel), 'qemu': str(args.qemu), 'cpus': args.cpus,
        'heap_end_mib': args.heap_end_mib,
        'mode': args.mode, 'sha256': hashlib.sha256(kernel.read_bytes()).hexdigest(),
    }, indent=2) + '\n')
    return kernel


def run_plan(args, kernel, name, plan):
    directory = args.output / name
    directory.mkdir(exist_ok=True)
    result = {'passed': False, 'phases': [], 'mode': args.mode, 'cpus': args.cpus,
              'sha256': hashlib.sha256(kernel.read_bytes()).hexdigest()}
    start = time.monotonic()
    command = [str(args.qemu), '-M', 'novae_2k0302', '-cpu', 'la464', '-m', '256M',
               '-kernel', str(kernel), '-display', 'none', '-serial', 'stdio',
               '-monitor', 'none', '-gdb', f'tcp:127.0.0.1:{args.port}']
    result['command'] = command
    with (directory / 'serial.log').open('w', buffering=1) as log:
        guest = pexpect.spawn(command[0], command[1:], encoding='utf-8', timeout=120)
        guest.logfile_read = log

        def expect(marker, timeout=180):
            outcome = guest.expect([marker, r'Kernel page fault|\[  ASSERT  \]|\[  FAILED  \]|SMART FAIL:|TEST: FAIL|PUBLIC WB: FAIL|Assertion failed|assertion failed',
                                    pexpect.TIMEOUT, pexpect.EOF], timeout=timeout)
            if outcome:
                raise RuntimeError(f'{marker}: match={outcome}, tail={guest.before[-1500:]}')
            return guest.before + guest.after

        try:
            if args.cpus > 1:
                expect(f'SMP: {args.cpus} CPUs online')
            expect('NOVAUI READY')
            guest.sendline('port_echo KMD_BOOT_SYNC')
            expect('PORT ECHO: KMD_BOOT_SYNC')
            expect(r'msh /[>]')
            for index, step in enumerate(plan):
                print(f'{args.mode}/{args.cpus}/{name}: {step["command"]}', flush=True)
                before = time.monotonic()
                guest.sendline(step['command'])
                output = expect(step['expect'], step.get('timeout', 300))
                token = f'KMD_SYNC_{index}'
                guest.sendline('port_echo ' + token)
                expect('PORT ECHO: ' + token)
                expect(r'msh /[>]')
                phase = dict(step, seconds=round(time.monotonic() - before, 2))
                if step['command'] == 'port_resources':
                    match = re.search(r'RESOURCES heap=(\d+) pages=(\d+)/(\d+)', output)
                    if not match:
                        raise RuntimeError('Missing resource counters')
                    phase['resources'] = list(map(int, match.groups()))
                    if 'baseline' not in result:
                        result['baseline'] = phase['resources']
                    elif step.get('same_resources') and phase['resources'] != result['baseline']:
                        raise RuntimeError(f'Resource leak: {phase["resources"]} != {result["baseline"]}')
                result['phases'].append(phase)
            result['passed'] = True
        except Exception as error:
            result['error'] = str(error)
            debugger = [str(args.toolchain / (args.prefix + 'gdb')), '-batch', str(kernel)]
            for instruction in ['set pagination off', f'target remote :{args.port}',
                                'info threads', 'thread apply all bt', 'monitor info registers', 'detach']:
                debugger += ['-ex', instruction]
            try:
                dump = subprocess.run(debugger, text=True, capture_output=True, timeout=30)
                (directory / 'gdb.txt').write_text(dump.stdout + dump.stderr)
            except subprocess.TimeoutExpired:
                pass
            raise
        finally:
            guest.close(force=True)
            result['seconds'] = round(time.monotonic() - start, 2)
            (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')


def run_faults(args, kernel):
    if args.mode != 'page':
        raise ValueError('Protection fault drills require PAGE mode')
    for fault, marker in [('ro', 'Kernel page fault:'), ('nx', 'Kernel page fault:'),
                          ('dmw', 'Address error:'), ('dma', 'Kernel page fault:'),
                          ('low', 'Kernel page fault:')]:
        result = {'fault': fault, 'passed': False,
                  'sha256': hashlib.sha256(kernel.read_bytes()).hexdigest()}
        path = args.output / ('fault-' + fault)
        path.mkdir(exist_ok=True)
        with (path / 'serial.log').open('w', buffering=1) as log:
            command = [str(args.qemu), '-M', 'novae_2k0302', '-cpu', 'la464', '-m', '256M',
                       '-kernel', str(kernel), '-display', 'none', '-serial', 'stdio', '-monitor', 'none']
            if fault == 'low':
                poison = path / 'page-zero.bin'
                poison.write_bytes(b'\xff' * 4096)
                command += ['-device', f'loader,file={poison},addr=0,force-raw=on']
            guest = pexpect.spawn(command[0], command[1:], encoding='utf-8', timeout=180)
            guest.logfile_read = log
            try:
                guest.expect('NOVAUI READY')
                guest.sendline('kernel_map_fault ' + fault)
                guest.expect('KERNEL MAP FAULT: armed ' + fault)
                match = guest.expect([marker, 'KERNEL MAP FAULT: FAIL', pexpect.TIMEOUT, pexpect.EOF])
                if match:
                    raise RuntimeError('Missing protection fault: ' + fault)
                guest.expect(r'\n')
                result['exception'] = marker + guest.before
                result['passed'] = True
                print('Protection fault PASS:', fault, flush=True)
            finally:
                guest.close(force=True)
                (path / 'result.json').write_text(json.dumps(result, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=['dmw', 'page'], required=True)
    parser.add_argument('--cpus', type=int, choices=[1, 4], default=4)
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--prefix', default='loongarch64-linux-musl-')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=12)
    parser.add_argument('--heap-end-mib', type=int, choices=[96, 128], default=128,
                        help='private stress fixture heap/page split; production layout is unchanged')
    parser.add_argument('--port', type=int, default=12392)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--faults-only', action='store_true')
    parser.add_argument('--plan', type=Path, help='Run an additional retained JSON command plan')
    args = parser.parse_args()
    for name in ['output', 'toolchain', 'qemu', 'application']:
        setattr(args, name, getattr(args, name).resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    if args.skip_build:
        metadata = json.loads((args.output / 'build.json').read_text())
        if (metadata['mode'] != args.mode or metadata['cpus'] != args.cpus or
                metadata.get('heap_end_mib', 96) != args.heap_end_mib):
            parser.error('Requested mode, CPU count or heap split differs from the retained build')
        kernel = Path(metadata['kernel'])
        if hashlib.sha256(kernel.read_bytes()).hexdigest() != metadata['sha256']:
            parser.error('Retained ELF changed; rebuild before recording new results')
    else:
        kernel = build(args)
    if args.build_only:
        return
    if args.faults_only:
        run_faults(args, kernel)
        return

    def step(command, marker):
        return {'command': command, 'expect': marker}

    if args.plan:
        plan = json.loads(args.plan.read_text())
        run_plan(args, kernel, args.plan.stem, plan)
        return
    plan = [step('kernel_map_test 64', f'KERNEL MAP TEST: PASS mode={args.mode.upper()}'),
            step('smp_test 1000000', f'SMP TEST: PASS cpus={args.cpus}'),
            step('smp_reap_test 5000', 'SMP REAP TEST: PASS'),
            step('smp_tlb_test 10000', 'SMP TLB TEST: PASS'),
            step('port_test 2000', 'KERNEL PORT TEST: PASS'),
            step('novaui_status', 'errors=0 .*active=1'),
            step('novaui_stop', 'NOVAUI STOPPED mode=user'),
            step('utest_run', r'\[\s+PASSED\s+\]\s+\[\s*result\s*\]\s+12 tests\.'),
            step('smart_install', 'SMART INSTALL: PASS'),
            step('audit_install', 'AUDIT USER installed'),
            step('/mnt/user.elf', 'SMART REGRESSION PASS'),
            step('/mnt/user.elf stress 4096', 'SMART STRESS PASS'),
            step('/mnt/user.elf parallel 4 1024', 'SMART PARALLEL PASS'),
            step('smart_memory_test 256', 'SMART MEMORY TEST: PASS'),
            step('/mnt/smart-x.elf rights', 'SMART EXTENDED PASS'),
            step('/mnt/smart-x.elf io 4', 'SMART EXTENDED PASS'),
            step('/mnt/smart-x.elf sync 4', 'SMART EXTENDED PASS'),
            step('/mnt/smart-x.elf memory 8', 'SMART EXTENDED PASS'),
            step('public_reclaim_wait', 'PUBLIC RECLAIM: SETTLED'),
            step('port_resources', r'msh /[>]')]
    for _ in range(3):
        plan += [step('kernel_map_test 32', 'KERNEL MAP TEST: PASS'),
                 step('/mnt/smart-x.elf memory 8', 'SMART EXTENDED PASS'),
                 step('smart_memory_test 64', 'SMART MEMORY TEST: PASS'),
                 step('public_reclaim_wait', 'PUBLIC RECLAIM: SETTLED'),
                 dict(step('port_resources', r'msh /[>]'), same_resources=True)]
    plan += [step('novaui_start', 'NOVAUI READY'),
             step('kernel_map_test 16', 'KERNEL MAP TEST: PASS'),
             step('novaui_status', 'errors=0 .*active=1')]
    if args.cpus == 1:
        plan = [entry for entry in plan if not entry['command'].startswith(('smp_', 'smart_memory_test'))]
    (args.output / 'stress-plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    run_plan(args, kernel, 'stress', plan)


if __name__ == '__main__':
    main()
