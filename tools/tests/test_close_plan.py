# SPDX-License-Identifier: GPL-3.0-or-later
"""Characterize multi-document close cancellation using the real controller."""
from pathlib import Path
import argparse, json, tempfile
from ide_test_process import write_profile, run_ide_case

REQUIRED={
    'Close plan stages discard without clearing a live document',
    'Prepared close plan preserves both dirty buffers',
    'Close plan rejects a changed revision before removal',
    'Project close rejects a document added after the decision set',
    'Production close No then Cancel keeps the window usable',
    'Production cancelled close retains unsaved text and dirty markers',
    'Session serialization does not discard an untitled buffer',
    'Close plan rejects a document destroyed during a decision',
}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--ide',type=Path,required=True);args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='redpanda-close-plan-') as temp:
        case=Path(temp);write_profile(case)
        result=run_ide_case(args.ide,case,{'REDPANDA_QA_CLOSE_PLAN_ONLY':'1'})
        report=case/'close-plan-result.json'
        if not report.exists(): raise RuntimeError(f'Missing report; exit={result.returncode}; {result.stderr.decode(errors="replace")}')
        data=json.loads(report.read_text(encoding='utf-8'))
        passed={c['name'] for c in data.get('checks',[]) if c.get('passed') is True}
        if result.returncode or data.get('failures')!=0 or not REQUIRED<=passed: raise RuntimeError(f'Close plan failed: {data}')
        print('PASS: close plan, real No/Cancel controller, dirty state and stale document conflicts')
if __name__=='__main__': main()
