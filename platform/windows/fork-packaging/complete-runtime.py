from pathlib import Path
import subprocess,os,re,shutil,json,argparse,hashlib
parser=argparse.ArgumentParser(description="Complete private Windows runtime dependencies")
parser.add_argument('--payload-root', required=True, type=Path)
parser.add_argument('--qt-prefix', required=True, type=Path)
parser.add_argument('--compiler-prefix', required=True, type=Path)
parser.add_argument('--portable-build', required=True, type=Path)
parser.add_argument('--installed-build', required=True, type=Path)
args=parser.parse_args()
base=args.payload_root
sdk=args.qt_prefix
compiler=args.compiler_prefix
env=os.environ.copy();env['PATH']=str(compiler/'bin')+';'+env.get('PATH','')
def imports(path):
 p=subprocess.run([str(compiler/'bin/objdump.exe'),'-p',str(path)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW,timeout=20)
 if p.returncode:raise RuntimeError('Cannot inspect '+str(path))
 return re.findall(r'DLL Name:\s*(\S+)',p.stdout.decode('utf-8',errors='replace'))

def digest(path):
 value=hashlib.sha256()
 with path.open('rb') as stream:
  for block in iter(lambda:stream.read(1024*1024),b''):value.update(block)
 return value.hexdigest()

def copy_notices(payload, build):
 app=payload/'app';license_root=payload/'licenses';license_root.mkdir(exist_ok=True)
 components={};versions={};origins={}
 rules=[(r'qt6svg\.dll|qsvg(?:icon)?\.dll','qt6-svg'),(r'qt6.*\.dll','qt6-base'),
        (r'lib(?:gcc_s_seh-1|stdc\+\+-6)\.dll','gcc-libs'),(r'libwinpthread-1\.dll','libwinpthread'),
        (r'libicu(?:dt|in|uc)\d+\.dll','icu'),(r'libb2-1\.dll','libb2'),
        (r'libbrotli(?:common|dec|enc)\.dll','brotli'),(r'libbz2-1\.dll','bzip2'),
        (r'libdouble-conversion\.dll','double-conversion'),(r'libffi-\d+\.dll','libffi'),
        (r'libfreetype-\d+\.dll','freetype'),(r'lib(?:gio|glib|gmodule|gobject)-2\.0-0\.dll','glib2'),
        (r'libgraphite2\.dll','graphite2'),(r'libharfbuzz-\d+\.dll','harfbuzz'),
        (r'libiconv-\d+\.dll','libiconv'),(r'libintl-\d+\.dll','gettext-runtime'),
        (r'libjpeg-\d+\.dll','libjpeg-turbo'),(r'libmd4c\.dll','md4c'),
        (r'libpcre2-\d+-0\.dll','pcre2'),(r'libpng\d+-\d+\.dll','libpng'),
        (r'libzstd\.dll','zstd'),(r'zlib1\.dll','zlib'),(r'lib(?:ssl|crypto)-\d+-x64\.dll','openssl')]
 for module in sorted(payload.rglob('*')):
  if not module.is_file():continue
  name=module.name.casefold();component=None
  if module.suffix.casefold()=='.dll':
   component=next((item for pattern,item in rules if re.fullmatch(pattern,name)),None)
   if component is None and 'plugins' in module.relative_to(app).parts:component='qt6-base'
   if component is None:raise RuntimeError('License mapping required for distributed DLL '+str(module))
  elif name=='astyle.exe':component='astyle'
  elif name in ('redpandaide.exe','redpandaide-core.exe','consolepauser.exe','redpanda-win-git-askpass.exe'):component='redpanda'
  elif module.suffix.casefold()=='.exe':raise RuntimeError('License mapping required for distributed EXE '+str(module))
  elif name.startswith('qt') and name.endswith('.qm'):component='qt6-translations'
  if component:components.setdefault(component,[]).append(module.relative_to(payload).as_posix())
 for component,modules in components.items():
  destination=license_root/component
  if component=='redpanda':
   destination.mkdir(exist_ok=True);shutil.copy2(app/'LICENSE',destination/'LICENSE')
   origins[component]='https://github.com/lellomele/RedPanda-CPP'
  elif component=='astyle':
   result=subprocess.run([str(app/'astyle.exe'),'--version'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                         creationflags=subprocess.CREATE_NO_WINDOW,timeout=10,check=True)
   version=re.search(rb'Artistic Style Version (\d+\.\d+\.\d+)',result.stdout+result.stderr)
   if not version:raise RuntimeError('Cannot identify distributed Artistic Style version')
   versions[component]=version.group(1).decode('ascii')
   source=Path(__file__).parent/'notices'/('astyle-'+versions[component])
   if not (source/'LICENSE.md').is_file():raise RuntimeError('Matching Artistic Style notice missing: '+str(source))
   shutil.copytree(source,destination,dirs_exist_ok=True)
   origins[component]='https://gitlab.com/saalen/astyle/-/tree/'+versions[component]
  elif component=='icu':
   majors={re.fullmatch(r'libicu(?:dt|in|uc)(\d+)\.dll',Path(module).name).group(1) for module in modules}
   if len(majors)!=1:raise RuntimeError('Mixed ICU majors in payload')
   major=next(iter(majors));candidates=[]
   for prefix in (sdk,compiler):
    if all((prefix/'bin'/Path(module).name).is_file() and digest(prefix/'bin'/Path(module).name)==digest(payload/module) for module in modules):
     candidates += list((prefix/'share'/'icu').glob(major+'.*/LICENSE'))
   if len(candidates)!=1:raise RuntimeError('A unique matching ICU notice is required: '+repr(candidates))
   source=candidates[0];versions[component]=source.parent.name
   destination.mkdir(exist_ok=True);shutil.copy2(source,destination/'LICENSE')
   origins[component]='https://github.com/unicode-org/icu/releases'
  else:
   source=next((prefix/'share'/'licenses'/component for prefix in (sdk,compiler)
                if (prefix/'share'/'licenses'/component).is_dir()),None)
   if source is None:raise RuntimeError('Required distributed license missing: '+component)
   shutil.copytree(source,destination,dirs_exist_ok=True)
   origins[component]='Qt SDK or MSYS2 package notice: '+component
 cache=(build/'CMakeCache.txt').read_text(encoding='utf-8')
 if 'LUA_ADDON:BOOL=ON' in cache:
  header=Path(__file__).resolve().parents[3]/'libs/lua/lua/lua.h'
  text=header.read_text(encoding='utf-8');start=text.rfind('/*',0,text.index('* Copyright (C) 1994-2023 Lua.org'))
  end=text.index('*/',start)+2;notice=text[start:end]
  if 'Permission is hereby granted' not in notice:raise RuntimeError('Lua MIT notice not found in corresponding sources')
  destination=license_root/'lua';destination.mkdir(exist_ok=True)
  (destination/'LICENSE.txt').write_text(notice+'\n',encoding='utf-8')
  versions['lua']='.'.join(re.search(r'#define LUA_VERSION_'+part+r'\s+"([^"]+)"',text).group(1) for part in ('MAJOR','MINOR','RELEASE'))
  components['lua']=['app/RedPandaIDE-core.exe (embedded Lua)'];origins['lua']='libs/lua/lua/lua.h; https://www.lua.org/license.html'
 index=[]
 for component,modules in sorted(components.items()):
  notices=sorted(p for p in (license_root/component).rglob('*') if p.is_file())
  if not notices or any(not p.read_bytes().strip() for p in notices):raise RuntimeError('Empty required license notice: '+component)
  index.append(dict(component=component,version=versions.get(component),modules=modules,origin=origins[component],
                    notices=[dict(path=p.relative_to(payload).as_posix(),sha256=digest(p)) for p in notices]))
 (license_root/'COMPONENTS.json').write_text(json.dumps(index,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 (license_root/'README.txt').write_text('English\nThird-party licenses and original notices are retained in the component folders.\nCOMPONENTS.json maps distributed modules to their notices and SHA-256 checksums.\nArtistic Style and embedded Lua use MIT; ICU includes its complete upstream notice.\nThe IDE, launcher and Console Pauser sources are in the Sources archive of this release.\nQt is dynamically linked; its DLLs may be replaced with compatible builds under their licenses.\n\nItaliano\nLe licenze e i notice originali sono conservati nelle cartelle dei componenti.\nCOMPONENTS.json associa moduli distribuiti, notice e impronte SHA-256.\nArtistic Style e Lua incorporato usano MIT; ICU include il notice upstream completo.\nI sorgenti di IDE, launcher e Console Pauser sono nello ZIP Sources della release.\nQt è collegato dinamicamente: le DLL sono sostituibili con build compatibili secondo le relative licenze.\n',encoding='utf-8')
 return index

system=Path(os.environ['SystemRoot'])/'System32'
for mode in ('portable','installed'):
 payload=base/mode;app=payload/'app'
 build=args.portable_build if mode=='portable' else args.installed_build
 shutil.copy2(build/'RedPandaIDE/RedPandaIDE.exe',app/'RedPandaIDE-core.exe')
 legacy=app/'RedPandaIDE.exe'
 if legacy.exists():legacy.unlink()
 queue=list(payload.rglob('*.exe'))+list(payload.rglob('*.dll'));seen=set();copied=[];unresolved=[]
 while queue:
  module=queue.pop(0)
  if str(module).casefold() in seen:continue
  seen.add(str(module).casefold())
  for dll in imports(module):
   if dll.lower().startswith(('api-ms-win-','ext-ms-win-')) or (system/dll).exists():continue
   # Qt plugins resolve their imports from the actual application directory.
   target=app/dll
   if target.exists():queue.append(target);continue
   source=next((folder/dll for folder in (sdk/'bin',compiler/'bin') if (folder/dll).exists()),None)
   if source is None:unresolved.append((str(module),dll));continue
   shutil.copy2(source,target);queue.append(target);copied.append(dll)
 if unresolved:raise RuntimeError('Missing dependencies: '+repr(unresolved))
 notice_index=copy_notices(payload,build)
 shutil.copy2(app/'LICENSE',payload/'LICENSE')
 (payload/'COPYRIGHT.txt').write_text('Red Panda C++\n\nModifiche del fork: Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)\nProgetto originale: Copyright (C) 2020-2026 Roy Qu e contributori.\n\nLicenza dell’IDE: GNU GPL versione 3 o successiva; vedere LICENSE.\nLe librerie distribuite hanno le rispettive licenze nella cartella licenses.\nI sorgenti del fork sono forniti nell’archivio Sources della stessa distribuzione.\n',encoding='utf-8')
 (payload/'LEGGIMI.txt').write_text('Red Panda C++ - fork Windows x64\n\nAvvia RedPandaIDE.exe nella cartella principale. La sottocartella app contiene\nRedPandaIDE-core.exe, le librerie private Qt 6.11.2 e i plugin dell’IDE.\nNon occorre installare Qt o impostare un percorso verso lo SDK di sviluppo.\n\n'+('Versione portabile: estrai tutta la cartella in un percorso scrivibile.\nLe preferenze e i dati di recupero sono conservati in app/config.\nPer trasferire le preferenze copia anche quella cartella.\n' if mode=='portable' else 'Versione installabile: il setup installa per l’utente corrente.\nLe preferenze e il recupero sono nel profilo utente, fuori dalla cartella del programma.\nLa disinstallazione conserva preferenze e file personali.\n')+'\nPer compilare e fare debug dei tuoi progetti, configura GCC/Clang e GDB/LLDB\nelle preferenze. Il compilatore non è incluso in questo pacchetto.\n\nCopyright del fork: Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)\nLicenze e crediti originali: vedere COPYRIGHT.txt, LICENSE e licenses.\n',encoding='utf-8')
 record=dict(mode=mode,privateRuntime='app',copiedDependencies=copied,inspectedModules=len(seen),unresolved=unresolved,licenseComponents=len(notice_index))
 (base.parent/('runtime-'+mode+'-verification.json')).write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
 print(mode, 'modules=',len(seen),'extra DLLs=',len(copied),'missing=',len(unresolved))