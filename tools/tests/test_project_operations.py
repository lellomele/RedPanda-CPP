# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise project resource commits and cancellation in an isolated IDE."""
from pathlib import Path
import argparse, json, tempfile
from ide_test_process import write_profile, run_ide_case

REQUIRED={
    'Failed project load does not register a folder watcher',
    'Failed project create does not register a folder watcher',
    'Closed project unit rename commits without an Editor',
    'Project rename preserves unit identity and settings',
    'Closed unit analysis completes',
    'Case-only open unit rename preserves editor association and unsaved buffer',
    'Project replacement honors destination-save cancellation',
    'Cancelled project rename preserves files and unit associations',
    'Cancelled project rename preserves unsaved destination editor',
    'Cancelled unit removal preserves membership dirty buffer flags and disk',
    'Failed save during unit removal preserves membership and unsaved document',
    'Failed trash never falls back to delete or discard the dirty editor',
    'Confirmed discard removes one unit without saving or a second close dialog',
    'Replayed removal cannot delete a replacement file or emit another removal',
    'External rename collision preserves both unit identities and original buffers',
    'External rename never binds a project unit to an unrelated destination buffer',
    'Explicit relocation recovery selects one buffer without discarding either document',
    'Project metadata candidate commits',
    'Atomic metadata reload preserves unresolved relocation and suspended association',
    'Compiler manager rejects unresolved project inputs before worker submission',
    'Failed project metadata save retains dirty state and previous file',
    'Layout failure cannot mark a partially saved project clean',
    'Project parser drains before teardown',
    'Background project reference delays only object destruction',
    'Late project release does not close a reopened unsaved editor',
    'Project destruction does not remove another workspace watcher',
    'Shared project factory has no QObject parent ownership',
    'Worker final release destroys Project on its owner thread',
}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--ide',type=Path,required=True);args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='redpanda-project-operations-') as temp:
        case=Path(temp);write_profile(case,completion=True)
        result=run_ide_case(args.ide,case,{'REDPANDA_QA_PROJECT_OPERATIONS':'1'})
        report=case/'project-operations-result.json'
        if not report.exists(): raise RuntimeError(f'Missing report; exit={result.returncode}; {result.stderr.decode(errors="replace")}')
        data=json.loads(report.read_text(encoding='utf-8'))
        passed={c['name'] for c in data.get('checks',[]) if c.get('passed') is True}
        if result.returncode or data.get('failures')!=0 or not REQUIRED<=passed: raise RuntimeError(f'Project operations failed: {data}')
        print('PASS: project rename, destination cancellation, metadata failures and unit identity')
if __name__=='__main__': main()
