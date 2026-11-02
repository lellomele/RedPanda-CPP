# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify recovery snapshots track committed editor contents and failed writes."""
from pathlib import Path
import argparse, json, tempfile
from ide_test_process import write_profile, run_ide_case

REQUIRED = {
    'Initial real recovery checkpoint is scheduled and committed',
    'setContent QString invalidates a cached dirty document in the real snapshot',
    'setContent QStringList invalidates the same cached dirty document',
    'Inflight document batch cannot schedule or replace a confirmed snapshot',
    'Outermost document commit makes the complete batch recoverable',
    'loadFromFile followed by dirty state replaces cached text in the real snapshot',
    'A failed actual recovery replacement retains the last confirmed bytes and state',
    'Retry after the replacement lock is released commits the latest full document',
}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ide',type=Path,required=True)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='redpanda-recovery-cache-') as temp:
        case=Path(temp);write_profile(case,recovery=True)
        result=run_ide_case(args.ide,case,{'REDPANDA_QA_RECOVERY_CACHE_ONLY':'1'})
        report=case/'recovery-cache-result.json'
        if not report.exists():
            raise RuntimeError(f'Missing recovery report; exit={result.returncode}; {result.stderr.decode(errors="replace")}')
        data=json.loads(report.read_text(encoding='utf-8'))
        checks=data.get('checks',[])
        passed={item['name'] for item in checks if item.get('passed') is True}
        if result.returncode or data.get('failures')!=0 or not REQUIRED<=passed or any(item.get('passed') is not True for item in checks):
            raise RuntimeError(f'Recovery cache workflow failed: {data}')
        print(f'PASS: {len(checks)} real recovery/cache checks, including failed atomic replacement and retry')

if __name__=='__main__':main()
