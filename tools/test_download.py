from pathlib import Path
import subprocess,os,time,hashlib,sys
r=Path(__file__).resolve().parents[1];u=r/'upstream';tc=r/'tools/llvm-mingw-20260616-ucrt-x86_64/bin';os.environ['PATH']=str(tc)+os.pathsep+os.environ['PATH'];os.environ['TMP']=os.environ['TEMP']=str(r/'work')
subprocess.run([str(tc/'clang++.exe'),'-std=c++14','-DNOMINMAX','-I'+str(u/'zlib'),str(r/'tests/download_smoke.cpp'),str(u/'gframe/card_download.cpp'),str(u/'bin/release/zlib.lib'),'-lwinhttp','-o',str(r/'work/download_smoke.exe')],check=True)
cache=r/'work'/('download-check-'+str(time.time_ns()));result=subprocess.run([str(r/'work/download_smoke.exe'),str(cache),str(r/'work/download-verified.ypk'),*(['resume'] if '--resume' in sys.argv else [])],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=600);report=result.stdout+result.stderr
if result.returncode==0:
 assert hashlib.sha256((r/'work/download-verified.ypk').read_bytes()).digest()==hashlib.sha256((r/'work/update-research/superpre.ypk').read_bytes()).digest()
 report+='PASS downloaded bytes match independently verified official package SHA256\n'
(r/'outputs'/('断点续传验证.txt' if '--resume' in sys.argv else '下载加速验证.txt')).write_text(report,encoding='utf-8');print(report);raise SystemExit(result.returncode)
