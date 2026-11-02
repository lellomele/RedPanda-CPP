#!/usr/bin/env python3
"""Test Structure interactions against an isolated, resource-bounded IDE."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from test_startup_structure_restore import write_profile
from test_rename_workflow import ExtendedJobLimits, ThreadEntry


def run_owned(ide, case, scenario, target=None):
    env = {key: value for key, value in os.environ.items() if not key.startswith('REDPANDA_QA_')}
    env['QT_QPA_PLATFORM'] = 'offscreen'
    temporary = case / 'tmp'
    temporary.mkdir()
    env['TMP'] = env['TEMP'] = env['TMPDIR'] = str(temporary)
    if scenario == 'freeze-reset':
        env['REDPANDA_QA_FREEZE_RESET'] = '1'
    elif scenario in ('tab-switch', 'tab-switch-split'):
        env['REDPANDA_QA_TAB_SWITCH'] = '1'
        if scenario == 'tab-switch-split':
            env['REDPANDA_QA_TAB_SWITCH_SPLIT'] = '1'
    else:
        env['REDPANDA_QA_PRIME_EXPAND'] = target.as_posix()
        env['REDPANDA_QA_PRIME_RACE'] = '1'

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel.CreateJobObjectW.restype = wintypes.HANDLE
    kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    kernel.SetInformationJobObject.restype = wintypes.BOOL
    kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    kernel.AssignProcessToJobObject.restype = wintypes.BOOL
    kernel.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Thread32First.argtypes = [wintypes.HANDLE, ctypes.POINTER(ThreadEntry)]
    kernel.Thread32Next.argtypes = [wintypes.HANDLE, ctypes.POINTER(ThreadEntry)]
    kernel.OpenThread.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenThread.restype = wintypes.HANDLE
    kernel.ResumeThread.argtypes = [wintypes.HANDLE]
    kernel.ResumeThread.restype = wintypes.DWORD
    job = kernel.CreateJobObjectW(None, None)
    if not job:
        raise ctypes.WinError(ctypes.get_last_error())
    process = None
    try:
        limits = ExtendedJobLimits()
        # Cap committed bytes and terminate only this test job on close.
        limits.basic.flags = 0x2000 | 0x100
        limits.processMemory = 1024 * 1024 * 1024
        if not kernel.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            raise ctypes.WinError(ctypes.get_last_error())
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        process = subprocess.Popen([str(ide), '--ui-test-output', str(case)], cwd=case, env=env,
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, startupinfo=startup,
            creationflags=subprocess.CREATE_NO_WINDOW | 0x00000004)
        if not kernel.AssignProcessToJobObject(job, wintypes.HANDLE(int(process._handle))):
            raise ctypes.WinError(ctypes.get_last_error())
        snapshot = kernel.CreateToolhelp32Snapshot(4, 0)
        if snapshot == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        resumed = 0
        try:
            entry = ThreadEntry()
            entry.size = ctypes.sizeof(entry)
            valid = kernel.Thread32First(snapshot, ctypes.byref(entry))
            while valid:
                if entry.ownerProcessId == process.pid:
                    thread = kernel.OpenThread(2, False, entry.threadId)
                    if not thread:
                        raise ctypes.WinError(ctypes.get_last_error())
                    try:
                        count = kernel.ResumeThread(thread)
                        if count == 0xffffffff:
                            raise ctypes.WinError(ctypes.get_last_error())
                        resumed += count > 0
                    finally:
                        kernel.CloseHandle(thread)
                valid = kernel.Thread32Next(snapshot, ctypes.byref(entry))
        finally:
            kernel.CloseHandle(snapshot)
        if not resumed:
            raise RuntimeError('Could not resume isolated IDE')
        try:
            stdout, stderr = process.communicate(timeout=60)
        except subprocess.TimeoutExpired as exc:
            kernel.TerminateJobObject(job, 1)
            process.communicate(timeout=10)
            progress = case / 'tab-switch-progress.jsonl'
            detail = f'; progress={progress.read_text(encoding="utf-8")[-4000:]}' if progress.is_file() else ''
            raise RuntimeError(f'{scenario}: IDE timed out after 60 seconds{detail}') from exc
        def read_report(name):
            path = case / name
            if not path.is_file():
                raise RuntimeError(f'{scenario}: no {name}; exit={process.returncode}; '
                                   f'stderr={stderr.decode(errors="replace")[-2000:]}')
            return json.loads(path.read_text(encoding='utf-8'))
        if process.returncode != 0:
            detail = ''
            for name in ('prime-result.json', 'prime-progress.jsonl', 'freeze-reset-ready.json', 'tab-switch-result.json'):
                path = case / name
                if path.exists(): detail += f' {name}={path.read_text(encoding="utf-8")[-3000:]}'
            raise RuntimeError(f'{scenario}: IDE exit={process.returncode};{detail}')
        if scenario == 'freeze-reset':
            ready = read_report('freeze-reset-ready.json')
            if ready != {'frozen': True, 'needsReparse': True, 'parserBusy': False}:
                raise RuntimeError(f'{scenario}: invalid preconditions: {ready}')
            if read_report('freeze-reset-result.json').get('returned') is not True:
                raise RuntimeError(f'{scenario}: handler did not return')
        elif scenario in ('tab-switch', 'tab-switch-split'):
            data = read_report('tab-switch-result.json')
            observations = data.get('observations', [])
            expected_count = 14 if scenario == 'tab-switch-split' else 10
            if (data.get('failures') != 0 or len(observations) != expected_count or
                    not all(item.get('passed') is True for item in observations)):
                raise RuntimeError(f'{scenario}: wrong active-file Structure: {data}')
            if {item.get('expectedSymbol') for item in observations} != {'', 'PrimeResult', 'OtherFixture'}:
                raise RuntimeError(f'{scenario}: one of the two files was never checked')
        else:
            data = read_report('prime-result.json')
            if data.get('ok') is not True:
                raise RuntimeError(f'{scenario}: failed: {data}')
            progress = (case / 'prime-progress.jsonl').read_text(encoding='utf-8').splitlines()
            phases = [json.loads(line) for line in progress]
            busy_cycles = {event.get('cycle') for event in phases if event.get('phase') == 'race-parser-busy'}
            if busy_cycles != {0, 1, 2}:
                raise RuntimeError(f'{scenario}: missing overlap cycles: {busy_cycles}')
        print(f'PASS: {scenario}', flush=True)
    finally:
        kernel.CloseHandle(job)
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=10)


def prepare_prime(case, split=False):
    profile = write_profile(case, share=True, restore=True)
    sources = case / 'files'
    sources.mkdir()
    (sources / 'first.cpp').write_text('struct OtherFixture { int value; };\n', encoding='utf-8')
    target = sources / 'structure_concurrency.cpp'
    text = ['struct PrimeResult { unsigned long long count; unsigned long long sum; };\n']
    text.extend(f'class Fixture{n} {{ public: int value() const {{ return {n}; }} }};\n' for n in range(200))
    target.write_text(''.join(text), encoding='utf-8')
    entries = []
    for name in ('first.cpp', target.name):
        entries.append({'filename': (sources/name).as_posix(), 'focused': name == target.name,
                        'onLeft': not split or name != target.name, 'readonly': False, 'fileType': 'CppSource',
                        'encodingOption': 'UTF-8', 'contextFile': '', 'caretX': 1,
                        'caretY': 1, 'top': 0, 'left': 0})
    if split:
        helper = sources / 'left_helper.cpp'
        helper.write_text('struct LeftHelper { int value; };\n', encoding='utf-8')
        entries.append({'filename': helper.as_posix(), 'focused': False,
                        'onLeft': True, 'readonly': False, 'fileType': 'CppSource',
                        'encodingOption': 'UTF-8', 'contextFile': '', 'caretX': 1,
                        'caretY': 1, 'top': 0, 'left': 0})
        empty = sources / 'empty.cpp'
        empty.write_text('', encoding='utf-8')
        entries.append({'filename': empty.as_posix(), 'focused': False,
                        'onLeft': False, 'readonly': False, 'fileType': 'CppSource',
                        'encodingOption': 'UTF-8', 'contextFile': '', 'caretX': 1,
                        'caretY': 1, 'top': 0, 'left': 0})
    (profile / 'lastopens.json').write_text(json.dumps({'files': entries}), encoding='utf-8')
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ide', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt': parser.error('Windows Job Objects are required')
    ide = args.ide.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='redpanda-concurrency-qa-') as temp:
        root = Path(temp).resolve()
        frozen = root / 'freeze-reset'
        frozen.mkdir()
        write_profile(frozen, share=True, restore=False)
        run_owned(ide, frozen, 'freeze-reset')
        prime = root / 'prime-race'
        prime.mkdir()
        target = prepare_prime(prime)
        run_owned(ide, prime, 'prime-race', target)
        switched = root / 'tab-switch'
        switched.mkdir()
        target = prepare_prime(switched)
        run_owned(ide, switched, 'tab-switch', target)
        split = root / 'tab-switch-split'
        split.mkdir()
        target = prepare_prime(split, split=True)
        run_owned(ide, split, 'tab-switch-split', target)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, AssertionError, RuntimeError, subprocess.SubprocessError) as error:
        print('FAIL:', error, file=sys.stderr)
        sys.exit(1)

