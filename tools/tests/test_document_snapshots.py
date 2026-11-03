# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify committed document versions through real editor/manager operations."""
from pathlib import Path
import argparse, json, tempfile
from ide_test_process import write_profile, run_ide_case

REQUIRED = {
    'Typing publishes complete text with a newer committed revision',
    'Analysis rejects a capture inside changing before reading the old buffer',
    'An actual parser job reads the committed open editor',
    'Undo publishes restored text at a new revision',
    'Redo publishes reapplied text at a new revision',
    'Commit notifications precede every status-driven capture',
    'An empty open buffer remains present and never falls back to disk',
    'A failed load keeps the previously committed revision and source',
    'Rename registers the new path before language/status side effects',
    'Save As registers its committed path before status and parser work',
    'Both panes recognize Windows case-insensitive paths',
    'A missing virtual path alias is an explicit conflict and cannot select another document',
    'Closing removes the live source while accepted snapshots remain valid',
    'Reopening the same path receives a new document identity',
}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ide',type=Path,required=True)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='redpanda-document-snapshots-') as temp:
        case=Path(temp);write_profile(case)
        result=run_ide_case(args.ide,case,{'REDPANDA_QA_DOCUMENT_STORE_ONLY':'1'})
        report=case/'document-store-result.json'
        if not report.exists():
            raise RuntimeError(f'Missing document report; exit={result.returncode}; {result.stderr.decode(errors="replace")}')
        data=json.loads(report.read_text(encoding='utf-8'))
        checks=data.get('checks',[])
        passed={item['name'] for item in checks if item.get('passed') is True}
        if result.returncode or data.get('failures')!=0 or not REQUIRED<=passed or any(item.get('passed') is not True for item in checks):
            raise RuntimeError(f'Document snapshot workflow failed: {data}')
        print(f'PASS: {len(checks)} committed document checks through the real editor and parser provider')

if __name__=='__main__':main()
