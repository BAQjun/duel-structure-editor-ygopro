from pathlib import Path
import os,subprocess,shutil
ROOT=Path(__file__).resolve().parents[1];UP=ROOT/'upstream';TC=ROOT/'tools'/'llvm-mingw-20260616-ucrt-x86_64'/'bin'
os.environ['PATH']=str(TC)+os.pathsep+os.environ['PATH'];os.environ['TMP']=os.environ['TEMP']=str(ROOT/'work')
objects=[str(p) for p in (UP/'obj'/'Release'/'YGOPro').glob('*.o') if p.name!='gframe.o']
libs=[str(UP/'bin'/'release'/f'{name}.lib') for name in ('ocgcore','lua','event','freetype','irrlicht','jpeg','png','zlib','sqlite','lzma')]
cmd=[str(TC/'clang++.exe'),'-std=c++14','-D_IRR_STATIC_LIB_', '-I'+str(UP/'irrlicht'/'include'),'-I'+str(UP/'sqlite3'),'-I'+str(UP/'lua'/'src'),str(ROOT/'tests'/'engine_smoke.cpp'),*objects,*libs,'-lwinhttp','-lws2_32','-liphlpapi','-lwinmm','-lopengl32','-lgdi32','-limm32','-lole32','-luuid','-o',str(ROOT/'work'/'engine_smoke.exe')]
subprocess.run(cmd,check=True)
run=Path(os.environ.get('DUEL_EDITOR_RUNTIME_ROOT',str(ROOT/'outputs'/'决构编辑器-v0.1.15')))
result=subprocess.run([str(ROOT/'work'/'engine_smoke.exe')],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
report=result.stdout+result.stderr
(ROOT/'outputs'/'引擎验证.txt').write_text(report,encoding='utf-8')
print(report)
print("Test process exit:",result.returncode)
raise SystemExit(result.returncode)
