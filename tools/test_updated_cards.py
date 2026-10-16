from pathlib import Path
import subprocess,os,shutil,time
r=Path(__file__).resolve().parents[1];u=r/'upstream';tc=r/'tools/llvm-mingw-20260616-ucrt-x86_64/bin';os.environ['PATH']=str(tc)+os.pathsep+os.environ['PATH'];os.environ['TMP']=os.environ['TEMP']=str(r/'work')
sandbox=r/'work'/('update-audit-'+str(time.time_ns()));sandbox.mkdir(exist_ok=True);shutil.copy2(r/'outputs/决构编辑器-v0.1/cards.cdb',sandbox/'cards.cdb');shutil.copytree(r/'outputs/决构编辑器-v0.1/script',sandbox/'script',dirs_exist_ok=True)
subprocess.run([str(r/'work/update_smoke.exe'),str(sandbox),str(r/'work/update-research'),'installonly'],check=True)
objects=[str(p) for p in (u/'obj/Release/YGOPro').glob('*.o') if p.name!='gframe.o'];libs=[str(u/'bin/release'/f'{name}.lib') for name in ('ocgcore','lua','event','freetype','irrlicht','jpeg','png','zlib','sqlite','lzma')]
subprocess.run([str(tc/'clang++.exe'),'-std=c++14','-DNOMINMAX','-D_IRR_STATIC_LIB_','-DNO_IRR_COMPILE_WITH_DIRECT3D_9_','-I'+str(u/'irrlicht/include'),'-I'+str(u/'lua/src'),'-I'+str(u/'sqlite3'),str(r/'tests/updated_cards_smoke.cpp'),*objects,*libs,'-lwinhttp','-lws2_32','-liphlpapi','-lwinmm','-lopengl32','-lgdi32','-limm32','-lole32','-luuid','-o',str(r/'work/updated_cards_smoke.exe')],check=True)
result=subprocess.run([str(r/'work/updated_cards_smoke.exe')],cwd=sandbox,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120);(r/'outputs/更新卡片效果验证.txt').write_text(result.stdout+result.stderr,encoding='utf-8');print(result.stdout);print(result.stderr[-5000:]);raise SystemExit(result.returncode)
