# SPDX-License-Identifier: GPL-3.0-or-later
"""Reject invalid GUI evidence and verify explicit platform-limited skips."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import tools.run_stabilization as runner
class GuiReportValidation(unittest.TestCase):
    def setUp(self):
        self.good={'language':'en','failures':0,'checks':[{'name':'Normal check','passed':True}]}
    def rejects(self, report, windows=True):
        with self.assertRaises(ValueError): runner.validate_gui_results(report,'en',windows)
    def approved(self):
        r=copy.deepcopy(self.good)
        r['checks'] += [{'name':name,'passed':True} for name in runner.CLIPBOARD_FAILURE_CHECKS]
        r['skipped']=[{'name':runner.CLIPBOARD_SKIP_NAME,'reason':runner.CLIPBOARD_SKIP_REASON}]
        return r
    def test_normal_report(self): self.assertEqual(runner.validate_gui_results(self.good,'en',True),(1,[]))
    def test_empty_checks_rejected(self): r=copy.deepcopy(self.good);r['checks']=[];self.rejects(r)
    def test_failed_check_rejected(self): r=copy.deepcopy(self.good);r['checks'][0]['passed']=False;self.rejects(r)
    def test_non_boolean_check_rejected(self): r=copy.deepcopy(self.good);r['checks'][0]['passed']=1;self.rejects(r)
    def test_failure_count_rejected(self): r=copy.deepcopy(self.good);r['failures']=1;self.rejects(r)
    def test_boolean_failure_count_rejected(self): r=copy.deepcopy(self.good);r['failures']=False;self.rejects(r)
    def test_wrong_language_rejected(self): r=copy.deepcopy(self.good);r['language']='it_IT';self.rejects(r)
    def test_approved_clipboard_skip_reported(self): r=self.approved();count,skips=runner.validate_gui_results(r,'en',True);self.assertEqual(count,4);self.assertEqual(skips,r['skipped'])
    def test_skip_on_linux_rejected(self): self.rejects(self.approved(),False)
    def test_wrong_skip_reason_rejected(self): r=self.approved();r['skipped'][0]['reason']='Other error';self.rejects(r)
    def test_wrong_skip_name_rejected(self): r=self.approved();r['skipped'][0]['name']='Compiler check';self.rejects(r)
    def test_two_skips_rejected(self): r=self.approved();r['skipped']*=2;self.rejects(r)
    def test_missing_negative_check_rejected(self): r=self.approved();r['checks'].pop();self.rejects(r)
    def test_skipped_check_cannot_be_passed(self): r=self.approved();r['checks'].append({'name':runner.CLIPBOARD_SKIP_NAME,'passed':True});self.rejects(r)
    def test_parametrized_duplicate_check_names_are_supported(self): r=copy.deepcopy(self.good);r['checks']*=2;self.assertEqual(runner.validate_gui_results(r,'en',True),(2,[]))
    def test_parametrized_report_structure_supported(self):
        r=copy.deepcopy(self.good)
        r['checks'] += [{'name':'Invalid theme rejected','passed':True} for _ in range(3)]
        r['checks'] += [{'name':f'Rendered adaptive icon: family/{i}','passed':True} for i in range(10)]
        self.assertEqual(runner.validate_gui_results(r,'en',True)[0],14)
    def test_compiler_request_requires_production_workflow(self):
        with self.assertRaises(ValueError): runner.validate_gui_results(self.good,'en',True,compiler=True)
    def test_git_request_requires_compiled_vcs_workflow(self):
        with self.assertRaises(ValueError): runner.validate_gui_results(self.good,'en',True,git=True)
    def test_all_requested_workflows_are_supported(self):
        r=copy.deepcopy(self.good)
        required=runner.COMPILER_WORKFLOW_CHECKS | runner.WINDOWS_MANAGED_PATH_CHECKS | runner.VCS_WORKFLOW_CHECKS
        r['checks'] += [{'name':name,'passed':True} for name in sorted(required)]
        self.assertEqual(runner.validate_gui_results(r,'en',True,compiler=True,git=True)[0],31)
    def test_windows_compiler_request_requires_managed_path_checks(self):
        r=copy.deepcopy(self.good)
        r['checks'] += [{'name':name,'passed':True} for name in sorted(runner.COMPILER_WORKFLOW_CHECKS)]
        self.assertEqual(runner.validate_gui_results(r,'en',False,compiler=True)[0],11)
        with self.assertRaises(ValueError): runner.validate_gui_results(r,'en',True,compiler=True)
if __name__=='__main__': unittest.main(verbosity=2)
