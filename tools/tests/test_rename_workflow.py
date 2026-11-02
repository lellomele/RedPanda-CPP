#!/usr/bin/env python3
"""Exercise the IDE rename workflow with disposable files and settings on Windows."""

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

REQUIRED_CHECKS = {
    'Successful rename updates declaration and reference',
    'Rename conflict reports error and preserves document',
    'Conflict dialog accepts an independent analysis request',
    'Queued analysis completes after rename conflict',
    'Busy rename reports error before asking for a name',
    'Busy rename did not secretly queue a parse',
    'Changed symbol aborts rename and preserves the intervening edit',
    *(f'{mode}: disk file is unchanged' for mode in ('success', 'conflict', 'busy', 'changed')),
}


class IoCounters(ctypes.Structure):
    _fields_ = [(name, ctypes.c_ulonglong) for name in (
        'readOperations', 'writeOperations', 'otherOperations', 'readBytes', 'writeBytes', 'otherBytes')]


class BasicJobLimits(ctypes.Structure):
    _fields_ = [('processTime', ctypes.c_longlong), ('jobTime', ctypes.c_longlong),
        ('flags', wintypes.DWORD), ('minimumWorkingSet', ctypes.c_size_t),
        ('maximumWorkingSet', ctypes.c_size_t), ('activeProcesses', wintypes.DWORD),
        ('affinity', ctypes.c_size_t), ('priority', wintypes.DWORD), ('scheduling', wintypes.DWORD)]


class ExtendedJobLimits(ctypes.Structure):
    _fields_ = [('basic', BasicJobLimits), ('io', IoCounters), ('processMemory', ctypes.c_size_t),
        ('jobMemory', ctypes.c_size_t), ('peakProcessMemory', ctypes.c_size_t),
        ('peakJobMemory', ctypes.c_size_t)]


class ThreadEntry(ctypes.Structure):
    _fields_ = [('size', wintypes.DWORD), ('usage', wintypes.DWORD), ('threadId', wintypes.DWORD),
        ('ownerProcessId', wintypes.DWORD), ('basePriority', wintypes.LONG),
        ('deltaPriority', wintypes.LONG), ('flags', wintypes.DWORD)]


def run_case(ide, case):
    env = os.environ.copy()
    for name in list(env):
        if name.startswith('REDPANDA_QA_'):
            del env[name]
    env['REDPANDA_QA_RENAME_ONLY'] = '1'
    env['QT_QPA_PLATFORM'] = 'offscreen'
    temporary = case / 'tmp'
    temporary.mkdir()
    env['TMP'] = env['TEMP'] = env['TMPDIR'] = str(temporary)

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
        # A hard per-process cap plus kill-on-close also confines descendants.
        limits.basic.flags = 0x2000 | 0x100
        limits.processMemory = 1024 * 1024 * 1024
        if not kernel.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            raise ctypes.WinError(ctypes.get_last_error())
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        process = subprocess.Popen([str(ide), '--ui-test-output', str(case)], cwd=case, env=env,
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, startupinfo=startup,
            creationflags=subprocess.CREATE_NO_WINDOW | 0x00000004)  # CREATE_SUSPENDED
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
            raise RuntimeError('Could not resume the isolated IDE process')
        try:
            stdout, stderr = process.communicate(timeout=60)
        except subprocess.TimeoutExpired as exc:
            kernel.TerminateJobObject(job, 1)
            process.communicate(timeout=10)
            raise RuntimeError('Isolated rename test timed out after 60 seconds') from exc
        report = case / 'rename-only-result.json'
        if not report.is_file():
            raise RuntimeError(f'No rename report; exit={process.returncode}; '
                               f'stderr={stderr.decode(errors="replace")[-2000:]}')
        data = json.loads(report.read_text(encoding='utf-8'))
        checks = data.get('checks')
        if process.returncode != 0 or data.get('failures') != 0 or not isinstance(checks, list):
            raise RuntimeError(f'Rename checks failed; exit={process.returncode}; report={data}')
        passed = {item.get('name') for item in checks if isinstance(item, dict) and item.get('passed') is True}
        if not REQUIRED_CHECKS <= passed:
            raise RuntimeError(f'Missing rename checks: {sorted(REQUIRED_CHECKS - passed)}')
        print(f'PASS: {len(checks)} isolated rename checks')
    finally:
        # Closing this private job affects only the process created above.
        kernel.CloseHandle(job)
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ide', required=True, type=Path, help='IDE built with GUI QA checks')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('This test uses Windows Job Objects')
    ide = args.ide.resolve(strict=True)
    if not ide.is_file():
        parser.error('--ide must point to an executable file')
    with tempfile.TemporaryDirectory(prefix='redpanda-rename-qa-') as temp:
        case = Path(temp).resolve()
        write_profile(case, share=False, restore=False)
        run_case(ide, case)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, AssertionError, RuntimeError, subprocess.SubprocessError) as error:
        print('FAIL:', error, file=sys.stderr)
        sys.exit(1)
