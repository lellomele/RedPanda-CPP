# SPDX-License-Identifier: GPL-3.0-or-later
"""Qualify identity-based project Save As and explicit Relink in an isolated IDE."""
from pathlib import Path
import argparse
import json
import tempfile
from ide_test_process import write_profile, run_ide_case

REQUIRED = {
    'Failed conversion preserves the current codec text Undo and original disk bytes',
    'Post-commit edits stay dirty and the receipt identifies the saved revision',
    'Undo returns to the committed revision and retry persists the newer edit',
    'Pre-commit edit invalidates the save plan without replacing disk bytes',
    'Pre-commit retarget cannot overwrite the newly selected destination',
    'Closing the view from a pre-commit observer returns without writing',
    'Template project establishes one canonical unit document binding',
    'Project Save As preserves unit identity document ID flags and inherited encoding',
    'Project Save As keeps explicit ANSI encoding and the same document identity',
    'Cancelled Save As preserves unit document revision metadata and unsaved text',
    'Save As rejects a document revision changed during its file dialog',
    'Failed text commit leaves document binding project metadata and old disk file intact',
    'Metadata failure after text commit reports partial success without renaming the live document',
    'Retry completes Save As while preserving the original unit and document identity',
    'Cancelled Relink preserves both documents unit metadata and source files',
    'Relink rejects unit settings changed during confirmation without replacing current edits',
    'Relink preserves the unit settings and binds only the explicitly accepted document',
    'Relink metadata reload retains flags encoding and no runtime document identity',
    'Reopening an old path creates a new document and cannot steal the unit binding',
    'Closing and reopening a project file replaces only its runtime document binding',
    'Standalone Save As cannot overwrite a closed file owned by another project unit',
    'Document file fixture closes its project without unexpected dialogs',
    'Closing the project during Save As preserves metadata and cancels stale document authority',
    'Native path identity keeps distinct Unicode project files and documents separate',
    'Ambiguous project paths are rejected before legacy conversion metadata or live state changes',
    'Validated legacy conversion preserves original units and existing backup files',
    'Rejected new project unit preserves existing membership and document without dereferencing null',
    'Save As failure preserves existing document for directory target and retries with coherent notifications',
    'Save As failure preserves existing document for missing parent and retries with coherent notifications',
    'Save As failure preserves new document for directory target and retries with coherent notifications',
    'Save As failure preserves new document for missing parent and retries with coherent notifications',
    'Save As encoding observers see committed identity type watcher and BOM bytes',
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ide', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='redpanda-document-files-') as temporary:
        case = Path(temporary)
        write_profile(case, completion=True)
        result = run_ide_case(args.ide, case, {'REDPANDA_QA_DOCUMENT_FILES': '1'})
        report = case / 'document-files-result.json'
        if not report.exists():
            progress = case / 'document-files-progress.log'
            if progress.is_file():
                print(progress.read_text(encoding='utf-8'))
            raise RuntimeError(f'Missing document file report; exit={result.returncode}; {result.stderr.decode(errors="replace")}')
        data = json.loads(report.read_text(encoding='utf-8'))
        passed = {item['name'] for item in data.get('checks', []) if item.get('passed') is True}
        if result.returncode or data.get('failures') != 0 or not REQUIRED <= passed:
            raise RuntimeError(f'Document file operations failed: {data}')
        print('PASS: template/project Save As, Unicode/ANSI, partial commits, cancellation and identity-based Relink')

if __name__ == '__main__':
    main()
