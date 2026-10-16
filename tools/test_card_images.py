from pathlib import Path
import subprocess,os
r=Path(__file__).resolve().parents[1];u=r/'upstream';tc=r/'tools/llvm-mingw-20260616-ucrt-x86_64/bin';os.environ['PATH']=str(tc)+os.pathsep+os.environ['PATH'];os.environ['TMP']=os.environ['TEMP']=str(r/'work')
subprocess.run([str(tc/'clang++.exe'),'-std=c++14','-DNOMINMAX','-D_IRR_STATIC_LIB_','-I'+str(u/'sqlite3'),'-I'+str(u/'zlib'),'-I'+str(u/'irrlicht/include'),str(r/'tests/card_images_smoke.cpp'),str(u/'gframe/card_images.cpp'),str(u/'gframe/card_download.cpp'),*[str(u/'bin/release'/f'{n}.lib') for n in ('sqlite','irrlicht','jpeg','png','zlib')],'-lwinhttp','-lwinmm','-lopengl32','-lgdi32','-limm32','-lole32','-luuid','-o',str(r/'work/card_images_smoke.exe')],check=True)
with (r/'outputs/全库卡图检查.txt').open('w',encoding='utf-8') as report:result=subprocess.run([str(r/'work/card_images_smoke.exe')],cwd=r/'outputs/决构编辑器-v0.1',stdout=report,stderr=subprocess.STDOUT,timeout=900,creationflags=subprocess.CREATE_NO_WINDOW)
print((r/'outputs/全库卡图检查.txt').read_text(encoding='utf-8')[-4000:]);raise SystemExit(result.returncode)
