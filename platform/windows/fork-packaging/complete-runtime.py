from pathlib import Path
import subprocess,os,re,shutil,json,argparse
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
 license_root=payload/'licenses';license_root.mkdir(exist_ok=True)
 for prefix in (sdk,compiler):
  for name in ('qt6-base','qt6-svg','qt6-translations','gcc-libs','winpthreads','libpng','libjpeg-turbo','freetype','harfbuzz','pcre2','zlib','zstd','double-conversion','brotli','bzip2','libb2','openssl','md4c','graphite2','icu','glib2','gettext-runtime','libffi','libiconv'):
   source=prefix/'share/licenses'/name
   if source.exists():shutil.copytree(source,license_root/name,dirs_exist_ok=True)
 shutil.copy2(app/'LICENSE',payload/'LICENSE')
 (payload/'COPYRIGHT.txt').write_text('Red Panda C++\n\nModifiche del fork: © 2006 Ing. prof. Raffaele Mele\nProgetto originale: Copyright (C) 2020-2026 Roy Qu e contributori.\n\nLicenza dell’IDE: GNU GPL versione 3 o successiva; vedere LICENSE.\nLe librerie distribuite hanno le rispettive licenze nella cartella licenses.\nI sorgenti del fork sono forniti nell’archivio Sources della stessa distribuzione.\n',encoding='utf-8')
 (payload/'LEGGIMI.txt').write_text('Red Panda C++ - fork Windows x64\n\nAvvia RedPandaIDE.exe nella cartella principale. La sottocartella app contiene\nRedPandaIDE-core.exe, le librerie private Qt 6.11.2 e i plugin dell’IDE.\nNon occorre installare Qt o impostare un percorso verso lo SDK di sviluppo.\n\n'+('Versione portabile: estrai tutta la cartella in un percorso scrivibile.\nLe preferenze e i dati di recupero sono conservati in app/config.\nPer trasferire le preferenze copia anche quella cartella.\n' if mode=='portable' else 'Versione installabile: il setup installa per l’utente corrente.\nLe preferenze e il recupero sono nel profilo utente, fuori dalla cartella del programma.\nLa disinstallazione conserva preferenze e file personali.\n')+'\nPer compilare e fare debug dei tuoi progetti, configura GCC/Clang e GDB/LLDB\nelle preferenze. Il compilatore non è incluso in questo pacchetto.\n\nCopyright del fork: © 2006 Ing. prof. Raffaele Mele\nLicenze e crediti originali: vedere COPYRIGHT.txt, LICENSE e licenses.\n',encoding='utf-8')
 record=dict(mode=mode,privateRuntime='app',copiedDependencies=copied,inspectedModules=len(seen),unresolved=unresolved)
 (base.parent/('runtime-'+mode+'-verification.json')).write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
 print(mode, 'modules=',len(seen),'extra DLLs=',len(copied),'missing=',len(unresolved))