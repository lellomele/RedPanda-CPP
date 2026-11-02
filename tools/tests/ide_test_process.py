# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded, isolated Windows IDE processes for application workflow tests."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import os
import subprocess
import time

class IoCounters(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ('readOperations','writeOperations','otherOperations','readBytes','writeBytes','otherBytes')]
class BasicLimits(ctypes.Structure):
    _fields_ = [('processTime',ctypes.c_longlong),('jobTime',ctypes.c_longlong),('flags',wintypes.DWORD),('minWorkingSet',ctypes.c_size_t),('maxWorkingSet',ctypes.c_size_t),('activeProcesses',wintypes.DWORD),('affinity',ctypes.c_size_t),('priority',wintypes.DWORD),('scheduling',wintypes.DWORD)]
class ExtendedLimits(ctypes.Structure):
    _fields_ = [('basic',BasicLimits),('io',IoCounters),('processMemory',ctypes.c_size_t),('jobMemory',ctypes.c_size_t),('peakProcessMemory',ctypes.c_size_t),('peakJobMemory',ctypes.c_size_t)]
class ThreadEntry(ctypes.Structure):
    _fields_ = [('size',wintypes.DWORD),('usage',wintypes.DWORD),('threadId',wintypes.DWORD),('ownerProcessId',wintypes.DWORD),('basePriority',wintypes.LONG),('deltaPriority',wintypes.LONG),('flags',wintypes.DWORD)]

def write_profile(case: Path, *, completion=False, recovery=False):
    profile=case/'profile'; profile.mkdir(parents=True)
    (profile/'redpandacpp.ini').write_text('[Environment]\nlanguage=en\nopen_files_in_single_instance=false\nrecovery_enabled='+str(recovery).lower()+'\n[Editor]\nauto_load_last_files=false\nparse_todos=false\n[CodeCompletion]\nenabled='+str(completion).lower()+'\nshare_parser=true\nclear_when_editor_hidden=true\n',encoding='utf-8')

def run_ide_case(ide: Path, case: Path, environment: dict, timeout=60, memory_mib=1024, *, platform='offscreen'):
    if os.name!='nt': raise RuntimeError('This workflow requires Windows Job Objects')
    if platform not in ('offscreen', 'windows'): raise ValueError('Unsupported QA platform')
    ide=ide.resolve(strict=True);case=case.resolve(strict=True)
    if not (case/'profile/redpandacpp.ini').is_file(): raise RuntimeError('Missing isolated profile')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateJobObjectW.argtypes=[ctypes.c_void_p,wintypes.LPCWSTR];kernel.CreateJobObjectW.restype=wintypes.HANDLE
    kernel.SetInformationJobObject.argtypes=[wintypes.HANDLE,ctypes.c_int,ctypes.c_void_p,wintypes.DWORD];kernel.SetInformationJobObject.restype=wintypes.BOOL
    kernel.AssignProcessToJobObject.argtypes=[wintypes.HANDLE,wintypes.HANDLE];kernel.AssignProcessToJobObject.restype=wintypes.BOOL
    kernel.TerminateJobObject.argtypes=[wintypes.HANDLE,wintypes.UINT]
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    kernel.CreateToolhelp32Snapshot.argtypes=[wintypes.DWORD,wintypes.DWORD];kernel.CreateToolhelp32Snapshot.restype=wintypes.HANDLE
    kernel.Thread32First.argtypes=[wintypes.HANDLE,ctypes.POINTER(ThreadEntry)];kernel.Thread32Next.argtypes=[wintypes.HANDLE,ctypes.POINTER(ThreadEntry)]
    kernel.OpenThread.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel.OpenThread.restype=wintypes.HANDLE
    kernel.ResumeThread.argtypes=[wintypes.HANDLE];kernel.ResumeThread.restype=wintypes.DWORD
    job=kernel.CreateJobObjectW(None,None)
    if not job: raise ctypes.WinError(ctypes.get_last_error())
    process=None
    stdout=case/'stdout.log';stderr=case/'stderr.log'
    try:
        limits=ExtendedLimits();limits.basic.flags=0x2000|0x100;limits.processMemory=memory_mib*1024*1024
        if not kernel.SetInformationJobObject(job,9,ctypes.byref(limits),ctypes.sizeof(limits)): raise ctypes.WinError(ctypes.get_last_error())
        env={k:v for k,v in os.environ.items() if not k.startswith('REDPANDA_QA_')}
        env.update(environment);env['QT_QPA_PLATFORM']=platform
        temporary=case/'tmp';temporary.mkdir()
        env['TMP']=env['TEMP']=env['TMPDIR']=str(temporary)
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        command=[str(ide),'--ui-test-output',str(case)]
        with stdout.open('wb') as out, stderr.open('wb') as err:
            process=subprocess.Popen(command,cwd=case,env=env,stdin=subprocess.DEVNULL,stdout=out,stderr=err,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW|0x00000004)
            if not kernel.AssignProcessToJobObject(job,wintypes.HANDLE(int(process._handle))): raise ctypes.WinError(ctypes.get_last_error())
            snapshot=kernel.CreateToolhelp32Snapshot(4,0)
            if snapshot==ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
            resumed=0
            try:
                entry=ThreadEntry();entry.size=ctypes.sizeof(entry)
                valid=kernel.Thread32First(snapshot,ctypes.byref(entry))
                while valid:
                    if entry.ownerProcessId==process.pid:
                        thread=kernel.OpenThread(2,False,entry.threadId)
                        if not thread: raise ctypes.WinError(ctypes.get_last_error())
                        try:
                            count=kernel.ResumeThread(thread)
                            if count==0xffffffff: raise ctypes.WinError(ctypes.get_last_error())
                            resumed+=count>0
                        finally: kernel.CloseHandle(thread)
                    valid=kernel.Thread32Next(snapshot,ctypes.byref(entry))
            finally: kernel.CloseHandle(snapshot)
            if not resumed: raise RuntimeError('Could not resume owned IDE test')
            deadline=time.monotonic()+timeout
            while process.poll() is None:
                if time.monotonic()>deadline or stdout.stat().st_size+stderr.stat().st_size>32*1024*1024:
                    kernel.TerminateJobObject(job,1);process.wait(timeout=10)
                    raise RuntimeError(f'IDE workflow exceeded its time/output budget; logs: {case}')
                time.sleep(0.05)
        def tail(path):
            with path.open('rb') as stream:
                stream.seek(max(0,path.stat().st_size-65536));return stream.read()
        return subprocess.CompletedProcess(command,process.returncode,tail(stdout),tail(stderr))
    finally:
        kernel.CloseHandle(job)
        if process is not None and process.poll() is None:
            process.kill();process.wait(timeout=10)
