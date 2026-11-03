# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual compiler/runner/debugger workflow independently of other UI checks."""
from pathlib import Path
import argparse
import json
import os
import shutil
import tempfile
from ide_test_process import write_profile, run_ide_case

REQUIRED = {
    'valid C++ emits a warning and creates its executable',
    'production warning reaches the panel with original GCC text',
    'managed-run no-console fixture builds with real GCC',
    'syntax error completes as a recoverable compile result',
    'error retains original GCC message in the panel',
    'panel and editor tooltip carry the same production diagnostic',
    'build succeeds after editing a previous compiler error',
    'missing header reaches GCC and retains its original diagnostic',
    'missing compiler is reported and its job returns to idle',
    'missing compiler preserves source and the prior successful artifact',
    'owned compiler fixture reaches its running marker within startup deadline',
    'real FileCompiler subprocess cancellation is bounded',
    'real GCC compilation succeeds after compiler-process cancellation',
    'production ProjectCompiler builds multiple units through Make',
    'linked multi-unit artifact exits normally with code zero',
    'project rebuild succeeds after a previous unit error',
    'owned GDB fixture stops at main and production MI parser reads stack frames',
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ide', required=True, type=Path)
    parser.add_argument('--language', required=True, choices=('en', 'it_IT'))
    parser.add_argument('--compiler', type=Path)
    args = parser.parse_args()
    compiler = args.compiler or os.environ.get('REDPANDA_QA_COMPILER') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('A real GCC/MinGW toolchain is required')
    compiler = Path(compiler).resolve(strict=True)
    completion = args.language == 'en'
    with tempfile.TemporaryDirectory(prefix='redpanda-production-compiler-') as temporary:
        case = Path(temporary)
        write_profile(case, completion=completion)
        ini = case / 'profile/redpandacpp.ini'
        profile = ini.read_text(encoding='utf-8').replace('language=en', 'language=' + args.language)
        profile = profile.replace('[Editor]\n', '[Editor]\nenable_autolink=true\n')
        ini.write_text(profile, encoding='utf-8')
        result = run_ide_case(args.ide, case, {
            'REDPANDA_QA_COMPILER_WORKFLOW_ONLY': '1',
            'REDPANDA_QA_COMPILER': str(compiler),
        }, timeout=110)
        report = case / 'compiler-workflow-result.json'
        if not report.is_file():
            raise RuntimeError(f'Missing production compiler report; exit={result.returncode}; {result.stderr.decode(errors="replace")}')
        data = json.loads(report.read_text(encoding='utf-8'))
        checks = data.get('checks', [])
        passed = {item['name'].removeprefix('Production compiler workflow: ')
                  for item in checks if item.get('passed') is True}
        required = REQUIRED | ({'Italian production error also has its local translation'} if args.language == 'it_IT' else set())
        if (result.returncode or data.get('failures') != 0 or not required <= passed
                or data.get('completionEnabled') is not completion or data.get('autolinkEnabled') is not True):
            message_file = case / 'compiler-workflow-messages.json'
            messages = message_file.read_text(encoding='utf-8') if message_file.is_file() else '[]'
            trace_file = case / 'compiler-workflow-trace.json'
            trace = json.loads(trace_file.read_text(encoding='utf-8')) if trace_file.is_file() else []
            failed = [item['name'] for item in checks if item.get('passed') is not True]
            raise RuntimeError(f'Compiler workflow failed ({args.language}); exit={result.returncode}; '
                               f'missing={sorted(required - passed)}; failed={failed}; '
                               f'completion={data.get("completionEnabled")}; autolink={data.get("autolinkEnabled")}; '
                               f'trace={trace}; dialogs={messages}')
        print(f'PASS: {len(checks)} production compiler checks; {args.language}; completion={completion}; autolink=True')

if __name__ == '__main__':
    main()
