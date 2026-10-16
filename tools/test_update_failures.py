from pathlib import Path
import subprocess,os,sqlite3,zipfile,shutil,time,hashlib
r=Path(__file__).resolve().parents[1];u=r/'upstream';tc=r/'tools/llvm-mingw-20260616-ucrt-x86_64/bin';os.environ['PATH']=str(tc)+os.pathsep+os.environ['PATH'];os.environ['TMP']=os.environ['TEMP']=str(r/'work')
subprocess.run([str(tc/'clang++.exe'),'-std=c++14','-DNOMINMAX','-I'+str(u/'sqlite3'),'-I'+str(u/'zlib'),'-I'+str(u/'irrlicht/include'),str(r/'tests/update_http_smoke.cpp'),str(u/'gframe/card_download.cpp'),str(u/'bin/release/sqlite.lib'),str(u/'bin/release/zlib.lib'),'-lwinhttp','-o',str(r/'work/update_http_smoke.exe')],check=True)
network=subprocess.run([str(r/'work/update_http_smoke.exe')],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=60);report=network.stdout+network.stderr
sandbox=r/'work'/('update-failures-'+str(time.time_ns()));sandbox.mkdir();shutil.copy2(r/'outputs/决构编辑器-v0.1/cards.cdb',sandbox/'cards.cdb');before=hashlib.sha256((sandbox/'cards.cdb').read_bytes()).digest()
fixtures=sandbox/'fixtures';fixtures.mkdir();shutil.copy2(r/'work/update-research/regular.cdb',fixtures/'regular.cdb')
cases=[('Unsafe ZIP path', [('repo/../escape.lua',b'evil')]),('Duplicate ZIP member',[('repo/c1.lua',b'x'),('repo/c1.lua',b'y')])]
for label,members in cases:
 with zipfile.ZipFile(fixtures/'scripts.zip','w',zipfile.ZIP_DEFLATED) as archive:
  for name,data in members:archive.writestr(name,data)
 result=subprocess.run([str(r/'work/update_smoke.exe'),str(sandbox),str(fixtures)],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=60)
 assert result.returncode!=0 and hashlib.sha256((sandbox/'cards.cdb').read_bytes()).digest()==before and not (sandbox/'.card-updates/current.txt').exists(),label
 report+='PASS '+label+' is rejected without changing card data\n'
(fixtures/'scripts.zip').write_bytes((r/'work/update-research/scripts.zip').read_bytes()[:-60])
result=subprocess.run([str(r/'work/update_smoke.exe'),str(sandbox),str(fixtures)],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=60)
assert result.returncode!=0 and hashlib.sha256((sandbox/'cards.cdb').read_bytes()).digest()==before
report+='PASS Truncated download is rejected without changing card data\n'
(r/'outputs/更新失败保护验证.txt').write_text(report,encoding='utf-8');print(report);raise SystemExit(network.returncode)
