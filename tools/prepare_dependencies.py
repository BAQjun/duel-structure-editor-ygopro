import concurrent.futures, hashlib, json, pathlib, shutil, urllib.request, tarfile, zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
TASKS=[
 ('lua','https://codeload.github.com/lua/lua/tar.gz/refs/tags/v5.4.8','lua-5.4.8',None),
 ('irrlicht','https://codeload.github.com/mercury233/irrlicht/tar.gz/refs/heads/main','irrlicht-main',None),
 ('event','https://github.com/libevent/libevent/releases/download/release-2.1.13-stable/libevent-2.1.13-stable.tar.gz','libevent-2.1.13-stable','f7e9383b8c0baa81b687e5b5eecc01beefaf1b19b64151d95ed61647fe7a315c'),
 ('freetype','https://codeload.github.com/freetype/freetype/tar.gz/refs/tags/VER-2-13-3','freetype-VER-2-13-3',None),
 ('jpeg','https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0.tar.gz','libjpeg-turbo-3.2.0','6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e'),
 ('png','https://codeload.github.com/pnggroup/libpng/tar.gz/refs/tags/v1.6.50','libpng-1.6.50',None),
 ('zlib','https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz','zlib-1.3.2','bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16'),
 ('sqlite3','http://www.sqlite.org/2026/sqlite-amalgamation-3530300.zip','sqlite-amalgamation-3530300','646421e12aac110282ef8cc68f1a62d4bb15fc7b8f09da0b53e29ee690500431'),
 ('lzma','https://github.com/tukaani-project/xz/releases/download/v5.8.3/xz-5.8.3.tar.gz','xz-5.8.3','3d3a1b973af218114f4f889bbaa2f4c037deaae0c8e815eec381c3d546b974a0'),
]
def get(task):
 name,url,folder,expected=task
 lock_file=ROOT/'work'/'dependency-sources.json'
 if expected is None and lock_file.exists():
  expected=next((r['sha256'] for r in json.loads(lock_file.read_text(encoding='utf-8')) if r['name']==name),None)
 target=ROOT/'upstream'/name
 path=ROOT/'work'/'downloads'/(name+('.zip' if url.endswith('.zip') else '.tar.gz'))
 print('Downloading '+name,flush=True)
 if not path.exists():
  with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'DuelStructureEditor/0.1'}),timeout=60) as r, path.open('wb') as out:
   while chunk:=r.read(1024*1024):out.write(chunk)
 with path.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
 if expected and digest!=expected:raise RuntimeError('SHA256 mismatch '+name)
 if not target.exists():
  dest=ROOT/'work'/'dependencies'/name;dest.mkdir(parents=True,exist_ok=True)
  if path.suffix=='.zip':
   with zipfile.ZipFile(path) as arc:arc.extractall(dest)
  else:
   with tarfile.open(path) as arc:arc.extractall(dest,filter='data')
  if name=='lua':
   (target/'src').mkdir(parents=True)
   for f in (dest/folder).iterdir():
    if f.is_file() and f.suffix in ('.c','.h'):shutil.copy2(f,target/'src'/f.name)
  else:shutil.move(str(dest/folder),str(target))
 print('Ready '+name,flush=True)
 return {'name':name,'url':url,'sha256':digest}
records=[];errors=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
 futures={pool.submit(get,t):t[0] for t in TASKS}
 for f in concurrent.futures.as_completed(futures):
  try:records.append(f.result())
  except Exception as e:errors.append(futures[f]+': '+str(e));print(errors[-1],flush=True)
(ROOT/'work'/'dependency-sources.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
if errors:raise RuntimeError('\n'.join(errors))
for folder in (ROOT/'upstream'/'premake').iterdir():
 if folder.is_dir():shutil.copytree(folder,ROOT/'upstream'/folder.name,dirs_exist_ok=True)
shutil.copy2(ROOT/'upstream'/'png'/'scripts'/'pnglibconf.h.prebuilt',ROOT/'upstream'/'png'/'pnglibconf.h')
shutil.copy2(ROOT/'upstream'/'jpeg'/'src'/'jversion.h.in',ROOT/'upstream'/'jpeg'/'src'/'jversion.h')
shutil.copy2(ROOT/'upstream'/'premake'/'event'/'msvc-event-config.h',ROOT/'upstream'/'event'/'include'/'event2'/'event-config.h')
shutil.copy2(ROOT/'upstream'/'event'/'WIN32-Code'/'nmake'/'evconfig-private.h',ROOT/'upstream'/'event'/'include'/'evconfig-private.h')
shutil.copytree(ROOT/'upstream'/'resource',ROOT/'upstream',dirs_exist_ok=True)
