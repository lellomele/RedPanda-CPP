#!/usr/bin/env python3
"""Exercise restored Structure state and busy-parser readiness in isolated profiles.

The IDE must include the opt-in REDPANDA_QA_RESTORED_FILE and
REDPANDA_QA_BUSY_ONLY checks. No user settings, projects or source files are read.
"""
from pathlib import Path
import argparse
import json
import os
import subprocess
import sys
import tempfile

CASES = (
    ('last-shared', 'last', True),
    ('first-shared', 'first', True),
    ('split-shared', 'split', True),
    ('last-private', 'last', False),
    ('split-private', 'split', False),
)
BUSY_CHECKS = {
    'Editor knows an idle scanned file does not need reparse',
    'Busy parser makes editor reparse check conservative',
    'Busy parser does not expose scanned-file state',
    'Overlapping request completes after conservative readiness check',
    'Editor readiness returns to idle after analysis',
}


def write_profile(case, share=True, restore=True):
    profile = case / 'profile'
    profile.mkdir(parents=True)
    # Explicit settings prevent compiler auto-detection and protect the user's profile.
    (profile / 'redpandacpp.ini').write_text(
        '[Environment]\n'
        'language=en\n'
        'theme=dark\n'
        'open_files_in_single_instance=false\n'
        'recovery_enabled=false\n'
        f'current_folder={case.as_posix()}\n'
        f'default_open_folder={case.as_posix()}\n'
        '[Editor]\n'
        f'auto_load_last_files={"true" if restore else "false"}\n'
        'parse_todos=false\n'
        '[UI]\n'
        'left_panel_index=3\n'
        'show_structure=true\n'
        '[CodeCompletion]\n'
        'enabled=true\n'
        f'share_parser={"true" if share else "false"}\n'
        'clear_when_editor_hidden=true\n', encoding='utf-8')
    return profile


def prepare(case, layout, share):
    profile = write_profile(case, share)
    sources = case / 'files'
    sources.mkdir()
    (sources/'first.cpp').write_text('struct FirstFixture { int value; };\n', encoding='utf-8')
    (sources/'second.cpp').write_text('struct SecondFixture { int value; };\n', encoding='utf-8')
    # A self-contained source with many declarations exercises the real parser.
    text = ['struct Config { int value; };\n']
    for n in range(200):
        text.append(f'class Fixture{n} {{ public: int value{n}() const {{ return {n}; }} }};\n')
    target = sources / 'startup_structure.cpp'
    target.write_text(''.join(text), encoding='utf-8')
    names = ['first.cpp', 'second.cpp', target.name]
    if layout == 'first':
        names = [target.name, 'first.cpp', 'second.cpp']
    files = []
    for name in names:
        files.append({'filename': (sources/name).as_posix(), 'focused': name == target.name,
            'onLeft': not (layout == 'split' and name != 'first.cpp'), 'readonly': False,
            'fileType': 'CppSource', 'encodingOption': 'UTF-8', 'contextFile': '',
            'caretX': 1, 'caretY': 1, 'top': 0, 'left': 0})
    (profile/'lastopens.json').write_text(json.dumps({'files': files}), encoding='utf-8')
    return target


def run_case(ide, case, target=None, timeout=55):
    env = os.environ.copy()
    env.pop('REDPANDA_QA_RESTORED_FILE', None)
    env.pop('REDPANDA_QA_BUSY_ONLY', None)
    env.pop('REDPANDA_QA_COMPILER', None)
    env.pop('REDPANDA_QA_GIT', None)
    env['QT_QPA_PLATFORM'] = 'offscreen'
    # Do not reuse the IDE startup lock in the user's normal temp directory.
    temporary = case / 'tmp'
    temporary.mkdir()
    env['TMP'] = env['TEMP'] = env['TMPDIR'] = str(temporary)
    if target is not None:
        env['REDPANDA_QA_RESTORED_FILE'] = str(target)
        report = case/'startup-restore-result.json'
    else:
        env['REDPANDA_QA_BUSY_ONLY'] = '1'
        report = case/'parser-busy-result.json'
    try:
        process = subprocess.run([str(ide), '--ui-test-output', str(case)], cwd=case,
            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            encoding='utf-8', errors='replace', timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f'{case.name}: IDE timed out after {timeout}s') from exc
    if not report.is_file():
        raise RuntimeError(f'{case.name}: no report; exit={process.returncode}; '
                           f'stderr={process.stderr[-2000:]}')
    data = json.loads(report.read_text(encoding='utf-8'))
    if process.returncode != 0:
        raise RuntimeError(f'{case.name}: IDE exit={process.returncode}; report={data}; '
                           f'stderr={process.stderr[-2000:]}')
    if target is not None:
        if (not all(data.get(k) is True for k in ('selected', 'modelAttached', 'parsed', 'configVisible'))
                or type(data.get('rootRows')) is not int or data['rootRows'] <= 0
                or Path(data['expectedFile']).resolve() != target.resolve()):
            raise RuntimeError(f'{case.name}: invalid restored Structure: {data}')
    else:
        checks = data.get('checks', [])
        if data.get('failures') != 0 or not isinstance(checks, list):
            raise RuntimeError(f'{case.name}: invalid busy-parser report: {data}')
        passed = {item.get('name') for item in checks if isinstance(item, dict) and item.get('passed') is True}
        if not BUSY_CHECKS <= passed:
            raise RuntimeError(f'{case.name}: missing busy-parser checks: {sorted(BUSY_CHECKS - passed)}')
    print('PASS:', case.name, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ide', required=True, type=Path, help='IDE executable built with GUI QA checks')
    args = parser.parse_args()
    ide = args.ide.resolve(strict=True)
    if not ide.is_file():
        parser.error('--ide must be an executable file')
    with tempfile.TemporaryDirectory(prefix='redpanda-structure-qa-') as temp:
        root = Path(temp).resolve()
        for name, layout, share in CASES:
            case = root/name
            case.mkdir()
            target = prepare(case, layout, share)
            run_case(ide, case, target)
        busy = root/'parser-busy'
        busy.mkdir()
        write_profile(busy, restore=False)
        run_case(ide, busy)
    print('PASS: restored Structure and parser readiness (6 isolated profiles)')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, AssertionError, RuntimeError) as error:
        print('FAIL:', error, file=sys.stderr)
        sys.exit(1)
