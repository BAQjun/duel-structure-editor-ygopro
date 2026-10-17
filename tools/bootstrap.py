import concurrent.futures, hashlib, json, pathlib, tarfile, urllib.request, zipfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
(ROOT/'work/downloads').mkdir(parents=True,exist_ok=True)
TASKS = [
 ('llvm-mingw', 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip', 'b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35', ROOT/'tools'),
 ('premake', 'https://github.com/premake/premake-core/releases/download/v5.0.0/premake-5.0.0-windows.zip', '15e63506eed5dfd526b96c8e957a7feff2be6cfe70f46e353bfe4813b82e2fdc', ROOT/'tools'/'premake'),
]
def get(task):
 name,url,expected,dest = task
 path=ROOT/'work'/'downloads'/url.split('/')[-1]
 print('Downloading '+name, flush=True)
 if not path.exists():
  with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'DuelStructureEditor/0.1'}),timeout=90) as response, path.open('wb') as out:
   while chunk:=response.read(1024*1024):out.write(chunk)
 digest=hashlib.file_digest(path.open('rb'),'sha256').hexdigest()
 if expected and digest!=expected: raise RuntimeError('SHA256 mismatch: '+name)
 dest.mkdir(parents=True,exist_ok=True)
 if path.suffix=='.zip':
  with zipfile.ZipFile(path) as archive: archive.extractall(dest)
 else:
  with tarfile.open(path) as archive: archive.extractall(dest,filter='data')
 print('Ready '+name+' '+digest,flush=True)
 return {'name':name,'url':url,'sha256':digest}
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
 results=list(pool.map(get,TASKS))
(ROOT/'work'/'dependency-downloads.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
