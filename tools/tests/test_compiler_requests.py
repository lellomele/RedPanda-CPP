# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise frozen compiler inputs, diagnostic provenance and process-tree retirement."""
from pathlib import Path
import argparse
import json
import os
import shutil
import tempfile
from ide_test_process import write_profile, run_ide_case

REQUIRED = {
    'GUI request capture and frozen compiler construction do not trigger lazy toolchain directory discovery',
    'Changing live compiler paths/options while the job awaits analysis does not change that job',
    "Compilation does not restore or overwrite the user's newer compiler preference edit",
    'The real executable produced by the captured request runs successfully',
    'Captured Unicode stdin text stays correct for ASCII ANSI UTF16 and UTF32 origin metadata',
    'Stdin UTF-8 transport remains correct when custom disk flags request an ANSI input encoding',
    'An accepted old stdin revision still runs but its errors cannot replace newer editor diagnostics',
    'Original header diagnostics remain in the panel but cannot mark a newer included-editor revision',
    'An unchanged accepted included-editor revision still receives its diagnostic hint',
    'Cancellation while awaiting preparation is distinct and preserves the previous executable',
    'A transitive autolink rule from the accepted configuration builds without an editor parser',
    'Compiler cancellation reaches the kernel tree acknowledgement before releasing the registered task',
    'A driver that exits before its child does not release the compiler registry gate',
    'The next job completes only after its child exits and the cancelled child never writes late output',
    'A generated output/source collision fails with the original source bytes preserved',
    'Window close waits for the registered compiler thread while the UI remains responsive',
    'Window close commits after the worker acknowledgement and the child cannot publish output',
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ide', type=Path, required=True)
    parser.add_argument('--compiler', type=Path)
    args = parser.parse_args()
    compiler = args.compiler or os.environ.get('REDPANDA_QA_COMPILER') or shutil.which('gcc')
    if not compiler:
        raise RuntimeError('A real GCC toolchain is required for compiler request qualification')
    compiler = Path(compiler).resolve(strict=True)
    helper = args.ide.resolve(strict=True).with_name('test-frozen-compilation.exe')
    if not helper.is_file():
        raise RuntimeError(f'Missing process-tree fixture: {helper}')
    with tempfile.TemporaryDirectory(prefix='redpanda-compiler-requests-') as temporary:
        case = Path(temporary)
        write_profile(case, completion=True)
        result = run_ide_case(args.ide, case, {
            'REDPANDA_QA_COMPILER_REQUESTS': '1',
            'REDPANDA_QA_COMPILER': str(compiler),
            'REDPANDA_QA_PROCESS_HELPER': str(helper),
        }, timeout=100)
        report = case / 'compiler-request-result.json'
        if not report.is_file():
            raise RuntimeError(f'Missing compiler request report; exit={result.returncode}; {result.stderr.decode(errors="replace")}')
        data = json.loads(report.read_text(encoding='utf-8'))
        checks = data.get('checks', [])
        passed = {item['name'] for item in checks if item.get('passed') is True}
        if result.returncode or data.get('failures') != 0 or not REQUIRED <= passed or len(checks) < 22:
            raise RuntimeError(f'Compiler request qualification failed: {data}')
        print(f'PASS: {len(checks)} real compiler input, Unicode, diagnostic and kernel process-tree checks')

if __name__ == '__main__':
    main()
