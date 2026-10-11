#!/usr/bin/env python3
"""Run functional, GUI and editing measurements in isolated test profiles."""
from pathlib import Path
import argparse,json,os,subprocess,sys,time

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',required=True,type=Path)
    parser.add_argument('--report-dir',required=True,type=Path)
    parser.add_argument('--ctest',default='ctest')
    parser.add_argument('--ui',action='store_true')
    parser.add_argument('--ide',type=Path)
    parser.add_argument('--compiler',type=Path)
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
            ok,_=run('ui-'+language,[ide,'--ui-test-output',profile],environment,120)
            if ok:
                try:
                    result=json.loads((profile/'ui-results.json').read_text(encoding='utf-8'))
                    passed=result['failures']==0 and result['language']==language and all(c['passed'] for c in result['checks'])
                    results[-1]['passed']=passed;results[-1]['checks']=len(result['checks'])
                except (OSError,ValueError,KeyError):
                    results[-1]['passed']=False;results[-1]['error']='missing or invalid GUI results'
    (report/'results.json').write_text(json.dumps(dict(schema=1,results=results),indent=2)+'\n',encoding='utf-8')
    for result in results:print(('PASS' if result['passed'] else 'FAIL')+': '+result['name'])
    return 0 if results and all(r['passed'] for r in results) else 1
if __name__=='__main__':sys.exit(main())
