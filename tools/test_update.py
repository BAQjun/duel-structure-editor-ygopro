from pathlib import Path
import subprocess,os,shutil,time,sys
ROOT=Path(__file__).resolve().parents[1];UP=ROOT/'upstream';TC=ROOT/'tools'/'llvm-mingw-20260616-ucrt-x86_64'/'bin'
os.environ['PATH']=str(TC)+os.pathsep+os.environ['PATH'];os.environ['TMP']=os.environ['TEMP']=str(ROOT/'work')
cmd=[str(TC/'clang++.exe'),'-std=c++14','-DNOMINMAX','-I'+str(UP/'sqlite3'),'-I'+str(UP/'zlib'),'-I'+str(UP/'irrlicht'/'include'),str(ROOT/'tests'/'update_smoke.cpp'),str(UP/'gframe'/'card_update_backend.cpp'),str(UP/'gframe'/'card_download.cpp'),str(UP/'bin'/'release'/'sqlite.lib'),str(UP/'bin'/'release'/'zlib.lib'),'-lwinhttp','-o',str(ROOT/'work'/'update_smoke.exe')]
subprocess.run(cmd,check=True)
sandbox=ROOT/'work'/('update-sandbox-'+str(time.time_ns()));sandbox.mkdir(exist_ok=True);shutil.copy2(ROOT/'outputs'/'决构编辑器-v0.1'/'cards.cdb',sandbox/'cards.cdb')
shutil.copytree(ROOT/'outputs'/'决构编辑器-v0.1'/'script',sandbox/'script',dirs_exist_ok=True)
result=subprocess.run([str(ROOT/'work'/'update_smoke.exe'),str(sandbox),str(ROOT/'work'/'update-research'),'network' if '--network' in sys.argv else 'install'],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=600)
report=result.stdout+result.stderr;(ROOT/'outputs'/('实际更新下载验证.txt' if '--network' in sys.argv else '卡片更新验证.txt')).write_text(report,encoding='utf-8');print(report);raise SystemExit(result.returncode)
