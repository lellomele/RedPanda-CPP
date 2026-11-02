#!/usr/bin/env python3
"""Test the actual CMake version resolver without touching a user's checkout."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile


def run(command, cwd, env):
    result = subprocess.run([str(x) for x in command], cwd=cwd, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, encoding='utf-8', errors='replace', timeout=30)
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}): {command[0]}\n{result.stderr[-2000:]}')
    return result.stdout


def prepare_version(directory, build=3824, prerelease=''):
    directory.mkdir(parents=True, exist_ok=True)
    data = {'major': 3, 'minor': 5, 'patch': 3, 'preRelease': prerelease}
    if build is not None:
        data['build'] = build
    (directory / 'version.json').write_text(json.dumps(data), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2],
                        help='Source root containing CMakeLists.txt')
    parser.add_argument('--cmake', default=shutil.which('cmake'), help='CMake executable')
    parser.add_argument('--git', default=shutil.which('git'), help='Optional Git executable')
    args = parser.parse_args()
    if not args.cmake:
        parser.error('CMake executable is required')
    cmake = Path(args.cmake).resolve(strict=True)
    git = Path(args.git).resolve(strict=True) if args.git else None
    source = args.source.resolve(strict=True)
    actual = (source / 'CMakeLists.txt').read_text(encoding='utf-8')
    start = actual.index('# read version from "version.json"')
    end = actual.index('if (UNIX AND NOT APPLE)', start)
    env = os.environ.copy()
    for name in tuple(env):
        if name.startswith(('GIT_CONFIG_KEY_', 'GIT_CONFIG_VALUE_')) or name in (
            'GIT_CONFIG_COUNT', 'GIT_CONFIG_GLOBAL', 'GIT_CONFIG_SYSTEM', 'GIT_DIR', 'GIT_WORK_TREE'):
            env.pop(name, None)
    env.update(GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_SYSTEM=os.devnull)
    # All generated repositories and files stay within this disposable directory.
    with tempfile.TemporaryDirectory(prefix='redpanda-version-test-') as temp:
        root = Path(temp).resolve(strict=True)
        if not root.is_relative_to(Path(tempfile.gettempdir()).resolve()):
            raise RuntimeError('Unexpected temporary directory')
        harness = root / 'resolve.cmake'
        harness.write_text(actual[start:end] + '\nmessage(STATUS "RESULT=${REDPANDA_VERSION}")\n', encoding='utf-8')

        def check(label, directory, expected, env_override=None):
            output = run([cmake, '-P', harness], directory, env_override or env)
            if '-- RESULT=' + expected not in output:
                raise RuntimeError(f'{label}: expected {expected}; got {output[-2000:]}')
            print('PASS:', label, expected, flush=True)

        archive = root / 'archive'
        prepare_version(archive)
        check('source archive', archive, '3.5.3.3824')
        old = root / 'legacy'
        prepare_version(old, None)
        check('legacy missing field', old, '3.5.3')
        malformed = root / 'malformed'
        prepare_version(malformed, 'bad')
        check('invalid fallback', malformed, '3.5.3')
        zero = root / 'zero'
        prepare_version(zero, 0)
        check('zero build number', zero, '3.5.3.0')
        prerelease = root / 'prerelease'
        prepare_version(prerelease, 3824, 'rc.1')
        check('prerelease suffix', prerelease, '3.5.3.3824-rc.1')
        gitless = root / 'without-git'
        prepare_version(gitless)
        (gitless / '.git').mkdir()
        empty_path = root / 'empty-path'
        empty_path.mkdir()
        gitless_env = env.copy()
        gitless_env['PATH'] = str(empty_path)
        check('git unavailable or invalid checkout', gitless, '3.5.3.3824', gitless_env)
        if git is not None:
            parent = root / 'parent'
            parent.mkdir()
            run([git, 'init', '-q'], parent, env)
            (parent / 'one').write_text('one', encoding='utf-8')
            run([git, 'add', 'one'], parent, env)
            identity = ['-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                        '-c', 'commit.gpgsign=false']
            run([git, *identity, 'commit', '-qm', 'first'], parent, env)
            nested = parent / 'extracted'
            prepare_version(nested)
            check('archive inside another repository', nested, '3.5.3.3824')
            checkout = root / 'checkout'
            prepare_version(checkout)
            run([git, 'init', '-q'], checkout, env)
            run([git, 'add', 'version.json'], checkout, env)
            run([git, *identity, 'commit', '-qm', 'first'], checkout, env)
            (checkout / 'two').write_text('two', encoding='utf-8')
            run([git, 'add', 'two'], checkout, env)
            run([git, *identity, 'commit', '-qm', 'second'], checkout, env)
            check('checkout prefers live Git count', checkout, '3.5.3.2')
            check('checkout when Git unavailable', checkout, '3.5.3.3824', gitless_env)
            linked = root / 'linked-worktree'
            run([git, 'worktree', 'add', '--detach', linked, 'HEAD'], checkout, env)
            if not (linked / '.git').is_file():
                raise RuntimeError('Expected .git file in linked worktree')
            check('linked worktree', linked, '3.5.3.2')
        else:
            print('SKIP: Git checkout, parent-repository and worktree cases (Git unavailable)', flush=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print('FAIL:', error, file=sys.stderr)
        sys.exit(1)
