from pathlib import Path
import urllib.request,time
ROOT=Path(__file__).resolve().parents[1];CACHE=ROOT/'work'/'update-research';CACHE.mkdir(parents=True,exist_ok=True)
def fetch(url,path):
 if path.exists():return
 started=time.monotonic()
 with urllib.request.urlopen(url,timeout=30) as response,path.with_suffix(path.suffix+'.partial').open('wb') as output:
  while True:
   block=response.read(65536)
   if not block:break
   output.write(block)
   if time.monotonic()-started>600:raise TimeoutError('Download exceeded ten minutes')
 path.with_suffix(path.suffix+'.partial').replace(path)
fetch('https://raw.githubusercontent.com/mycard/ygopro-database/master/locales/zh-CN/cards.cdb',CACHE/'regular.cdb')
fetch('https://codeload.github.com/Fluorohydride/ygopro-scripts/zip/refs/heads/master',CACHE/'scripts.zip')
fetch('https://cdntx.moecube.com/ygopro-super-pre/data/latest-tag.txt',CACHE/'latest-tag.txt')
tag=(CACHE/'latest-tag.txt').read_text(encoding='utf-8').strip()
assert tag.startswith('https://cdntx.moecube.com/ygopro-super-pre/archive/') and tag.endswith('.ypk')
fetch(tag,CACHE/'superpre.ypk')
import zipfile
with zipfile.ZipFile(CACHE/'superpre.ypk') as package:
 for name in ('test-release.cdb','test-update.cdb'):(CACHE/name).write_bytes(package.read(name))
print('Verified source fixtures downloaded to',CACHE)
