#!/usr/bin/env python3
"""Run functional, GUI and editing measurements in isolated test profiles."""
from pathlib import Path
import argparse,json,os,subprocess,sys,time

CLIPBOARD_SKIP_NAME = 'Clipboard contains exact GCC message'
CLIPBOARD_SKIP_REASON = 'Native Windows OpenClipboard(NULL) returned ERROR_ACCESS_DENIED (5) in the restricted QA process.'
CLIPBOARD_FAILURE_CHECKS = {
    'Denied native clipboard reports a recoverable translated copy failure',
    'Denied native clipboard preserves the original diagnostic and selection',
    'Denied native clipboard copy can be retried',
}

COMPILER_WORKFLOW_CHECKS = {
    'Production compiler workflow: ' + name for name in (
        'configured GCC is an existing executable',
        'valid C++ emits a warning and creates its executable',
        'production ExecutableRunner completes without a visible console',
        'real slow runner cancellation is bounded and joins its worker',
        'stop immediately after start is not lost',
        'real FileCompiler subprocess cancellation is bounded',
        'real GCC compilation succeeds after compiler-process cancellation',
        'project workflow has the real Make executable',
        'production ProjectCompiler builds multiple units through Make',
        'project rebuild succeeds after a previous unit error',
    )
}
WINDOWS_MANAGED_PATH_CHECKS = {
    'Production compiler workflow: managed run retains the compiler runtime path',
    'Production compiler workflow: managed run excludes IDE app and private library paths',
}
VCS_WORKFLOW_CHECKS = {
    'VCS smoke has a real Git executable',
    'VCS smoke initializes and recognizes an offline repository',
    'Git menu is available with VCS enabled',
    'Git staging QAction invokes its connected slot',
    'Canceled Git restore preserves working tree bytes',
    'Confirmed Git restore QAction invokes its connected slot',
    'Confirmed Git restore preserves unsaved editor and caret',
} | {'Git menu action and implemented slot: ' + name for name in (
    'Create_Repository', 'Add_Files', 'Commit', 'Restore', 'Branch', 'Merge', 'Log',
    'Remotes', 'Fetch', 'Pull', 'Push',
)}

def validate_gui_results(result, language, windows, compiler=False, git=False):
    """Accept only successful checks and the explicitly verified Windows skip."""
    if not isinstance(result, dict) or result.get('language') != language:
        raise ValueError('unexpected GUI report language')
    if type(result.get('failures')) is not int or result['failures'] != 0:
        raise ValueError('GUI report contains failures')
    checks = result.get('checks')
    if not isinstance(checks, list) or not checks:
        raise ValueError('GUI checks must be a nonempty array')
    names = set()
    for check in checks:
        if not isinstance(check, dict) or check.get('passed') is not True:
            raise ValueError('GUI check failed or has an invalid result')
        name = check.get('name')
        if not isinstance(name, str) or not name:
            raise ValueError('GUI check has a missing name')
        names.add(name)
    required = set()
    if compiler:
        required.update(COMPILER_WORKFLOW_CHECKS)
        if windows:
            required.update(WINDOWS_MANAGED_PATH_CHECKS)
    if git:
        required.update(VCS_WORKFLOW_CHECKS)
    missing = required - names
    if missing:
        raise ValueError('requested GUI workflow was not executed: ' + ', '.join(sorted(missing)))
    skipped = result.get('skipped', [])
    if not isinstance(skipped, list) or len(skipped) > 1:
        raise ValueError('unexpected GUI skipped checks')
    if skipped:
        if not windows or skipped[0] != dict(name=CLIPBOARD_SKIP_NAME, reason=CLIPBOARD_SKIP_REASON):
            raise ValueError('GUI skip is not authorized for this platform and reason')
        if CLIPBOARD_SKIP_NAME in names or not CLIPBOARD_FAILURE_CHECKS.issubset(names):
            raise ValueError('clipboard skip lacks its recoverable failure checks')
    return len(checks), skipped

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',required=True,type=Path)
    parser.add_argument('--report-dir',required=True,type=Path)
    parser.add_argument('--ctest',default='ctest')
    parser.add_argument('--ui',action='store_true')
    parser.add_argument('--ide',type=Path)
    parser.add_argument('--compiler',type=Path)
    parser.add_argument('--git',type=Path)
    parser.add_argument('--benchmark',action='store_true')
    args=parser.parse_args()
    build=args.build_dir.resolve();report=args.report_dir.resolve()
    report.mkdir(parents=True,exist_ok=True)
    results=[]
    def run(name,command,env,timeout):
        start=time.monotonic()
        try:
            process=subprocess.run([str(c) for c in command],env=env,capture_output=True,timeout=timeout)
            (report/(name+'.stdout.log')).write_bytes(process.stdout)
            (report/(name+'.stderr.log')).write_bytes(process.stderr)
            passed=process.returncode==0
            results.append(dict(name=name,passed=passed,exitCode=process.returncode,seconds=time.monotonic()-start))
            return passed,process.stdout
        except subprocess.TimeoutExpired as error:
            (report/(name+'.stdout.log')).write_bytes(error.stdout or b'')
            (report/(name+'.stderr.log')).write_bytes(error.stderr or b'')
            results.append(dict(name=name,passed=False,error='timeout',seconds=time.monotonic()-start))
            return False,b''
        except OSError as error:
            results.append(dict(name=name,passed=False,error=str(error),seconds=time.monotonic()-start))
            return False,b''
    headless=os.environ.copy();headless['QT_QPA_PLATFORM']='offscreen'
    run('functional',[args.ctest,'--test-dir',build,'--output-on-failure','--timeout','120','--output-junit',report/'functional.xml'],headless,600)
    suffix='.exe' if os.name=='nt' else ''
    if args.benchmark:
        ok,data=run('editing-performance',[build/'libs/qsynedit'/('benchmark-qsynedit'+suffix),'--lines','3000,30000,100000','--samples','5','--edits','40'],headless,180)
        if ok:
            try:
                json.loads(data)
                (report/'editing-performance.json').write_bytes(data)
            except ValueError:
                results[-1]['passed']=False;results[-1]['error']='invalid benchmark JSON'
    if args.ui:
        ide=(args.ide or build/'RedPandaIDE'/('RedPandaIDE'+suffix)).resolve()
        for language in ('en','it_IT'):
            profile=report/('ui-'+language)
            # Avoid reusing history, snapshots and artifacts from a previous run.
            if profile.exists():
                results.append(dict(name='ui-'+language,passed=False,error='profile already exists; choose a fresh report directory'))
                continue
            (profile/'profile').mkdir(parents=True)
            config='[Environment]\nlanguage='+language+'\ntheme=midnight\nopen_files_in_single_instance=false\nrecovery_enabled=true\nrecovery_interval=20\n[Editor]\nauto_load_last_files=false\n[CodeCompletion]\nenabled='+('false' if language=='it_IT' else 'true')+'\n'
            (profile/'profile/redpandacpp.ini').write_text(config,encoding='utf-8')
            tools=[dict(id='qa-legacy',title='Rimuovi compilato',program='del',workingDirectory='<SOURCEPATH>',parameters='/q /f <EXENAME>',inputOrigin=0,outputTarget=1,isUTF8=False),dict(id='qa-custom',title='QA custom tool',program='echo',workingDirectory='<SOURCEPATH>',parameters='QA',inputOrigin=0,outputTarget=1,isUTF8=False)]
            (profile/'profile/tools.json').write_text(json.dumps(tools),encoding='utf-8')
            environment=os.environ.copy()
            environment['QT_QPA_PLATFORM']='windows' if os.name=='nt' else 'offscreen'
            if args.compiler:environment['REDPANDA_QA_COMPILER']=str(args.compiler.resolve())
            if args.git:environment['REDPANDA_QA_GIT']=str(args.git.resolve())
            ok,_=run('ui-'+language,[ide,'--ui-test-output',profile],environment,120)
            if ok:
                try:
                    result=json.loads((profile/'ui-results.json').read_text(encoding='utf-8'))
                    check_count,skipped=validate_gui_results(result,language,os.name=='nt',compiler=bool(args.compiler),git=bool(args.git))
                    results[-1]['passed']=True;results[-1]['checks']=check_count
                    results[-1]['skipped']=skipped
                except (OSError,ValueError,KeyError):
                    results[-1]['passed']=False;results[-1]['error']='missing or invalid GUI results'
    (report/'results.json').write_text(json.dumps(dict(schema=1,results=results),indent=2)+'\n',encoding='utf-8')
    for result in results:
        print(('PASS' if result['passed'] else 'FAIL')+': '+result['name'])
        for skipped in result.get('skipped',[]):
            print('  SKIP: '+skipped['name']+' — '+skipped['reason'])
    return 0 if results and all(r['passed'] for r in results) else 1
if __name__=='__main__':sys.exit(main())
