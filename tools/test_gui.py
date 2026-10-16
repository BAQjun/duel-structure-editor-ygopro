from pathlib import Path
import os,subprocess,sys,shutil,sqlite3,zipfile
ROOT=Path(__file__).resolve().parents[1];UP=ROOT/'upstream';TC=ROOT/'tools'/'llvm-mingw-20260616-ucrt-x86_64'/'bin'
os.environ['PATH']=str(TC)+os.pathsep+os.environ['PATH'];os.environ['TMP']=os.environ['TEMP']=str(ROOT/'work')
includes=[UP/'lua'/'src',UP/'event'/'include',UP/'freetype'/'custom',UP/'freetype'/'include',UP/'sqlite3',UP/'irrlicht'/'include',UP/'jpeg'/'src',UP/'png',UP/'lzma'/'src'/'liblzma'/'api',UP/'zlib']
base=[str(TC/'clang++.exe'),'-std=c++14','-DNOMINMAX','-D_IRR_STATIC_LIB_','-DNO_IRR_COMPILE_WITH_DIRECT3D_9_','-DDUEL_EDITOR_GUI_TEST',*[ '-I'+str(p) for p in includes]]
subprocess.run([*base,'-c',str(UP/'gframe'/'game.cpp'),'-o',str(ROOT/'work'/'game-gui-test.o')],check=True)
subprocess.run([*base,'-c',str(UP/'gframe'/'card_update.cpp'),'-o',str(ROOT/'work'/'card-update-gui-test.o')],check=True)
os.environ['DUEL_EDITOR_UPDATE_FIXTURE']=str(ROOT/'work'/'update-research')

# Independently compare installed rows with the fixture, retaining the user's active version.
run_root=Path(os.environ.get('DUEL_EDITOR_RUNTIME_ROOT',str(ROOT/'outputs'/'决构编辑器-v0.1.16')));active=run_root;marker=run_root/'.card-updates/current.txt'
if marker.exists():
 version=marker.read_text(encoding='utf-8').strip()
 if version not in ('base','legacy'):active=run_root/'.card-updates/versions'/version
 elif version=='legacy':active=run_root/'.card-updates/active'
def rows(path):
 with sqlite3.connect(path) as db:
  sql='select '+','.join('datas.'+n for n in ['id','ot','alias','setcode','type','atk','def','level','race','attribute','category'])+','+','.join('texts.'+n for n in ['name','desc']+['str'+str(i) for i in range(1,17)])+' from datas join texts using(id)'
  return {row[0]:tuple('' if x is None else str(x) for x in row) for row in db.execute(sql)}
remote=rows(ROOT/'work/update-research/regular.cdb')
with zipfile.ZipFile(ROOT/'work/update-research/superpre.ypk') as pack:
 for n in sorted(pack.namelist()):
  if n.endswith('.cdb'):
   temp=ROOT/'work/gui-pre-reference.cdb';temp.write_bytes(pack.read(n));remote.update(rows(temp))
local=rows(active/'cards.cdb')
if (active/'superpre.ypk').exists():
 with zipfile.ZipFile(active/'superpre.ypk') as pack:
  for n in sorted(pack.namelist()):
   if n.endswith('.cdb'):
    temp=ROOT/'work/gui-local-pre-reference.cdb';temp.write_bytes(pack.read(n));local.update(rows(temp))
missing=sum(code not in local for code in remote);changed=sum(code in local and local[code]!=row for code,row in remote.items())
os.environ['DUEL_EDITOR_EXPECTED_MISSING']=str(missing);os.environ['DUEL_EDITOR_EXPECTED_DIFFERENCES']=str(missing+changed)

objects=[str(p) for p in (UP/'obj'/'Release'/'YGOPro').glob('*.o') if p.name not in ('gframe.o','game.o','card_update.o')]
libs=[str(UP/'bin'/'release'/f'{name}.lib') for name in ('ocgcore','lua','event','freetype','irrlicht','jpeg','png','zlib','sqlite','lzma')]
subprocess.run([*base,str(ROOT/'tests'/('gui_summon_history.cpp' if '--summon-history' in sys.argv else 'gui_sync_v015.cpp' if '--sync' in sys.argv else 'gui_perspective.cpp' if '--perspective' in sys.argv else 'gui_v04.cpp')),str(ROOT/'work'/'game-gui-test.o'),str(ROOT/'work'/'card-update-gui-test.o'),*objects,*libs,'-lwinhttp','-lws2_32','-liphlpapi','-lwinmm','-lopengl32','-lgdi32','-limm32','-lole32','-luuid','-o',str(ROOT/'work'/'gui_smoke.exe')],check=True)
image_path=Path(os.environ.get('DUEL_EDITOR_RUNTIME_ROOT',str(ROOT/'outputs'/'决构编辑器-v0.1.16'))) / '.card-images'/'pics'/'69272449.jpg'
backup=ROOT/'work'/'live-image-69272449.jpg'
if '--live-image' in sys.argv:
 assert image_path.resolve().is_relative_to(ROOT.resolve()) and backup.resolve().is_relative_to(ROOT.resolve())
 shutil.copy2(image_path,backup);image_path.unlink()
report_path=ROOT/'outputs'/('YGO-召唤素材验证.txt' if '--summon-history' in sys.argv else 'YGO-v015-界面验证.txt' if '--sync' in sys.argv else '视角与独立时点验证.txt' if '--perspective' in sys.argv else '界面验证.txt')
try:
 with report_path.open('w',encoding='utf-8') as live_report:
  result=subprocess.run([str(ROOT/'work'/'gui_smoke.exe')],cwd=Path(os.environ.get('DUEL_EDITOR_RUNTIME_ROOT',str(ROOT/'outputs'/'决构编辑器-v0.1.16'))),stdout=live_report,stderr=subprocess.STDOUT,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
finally:
 if '--live-image' in sys.argv and not image_path.exists():shutil.copy2(backup,image_path)
report=report_path.read_text(encoding='utf-8',errors='replace')
report_path.write_text(report,encoding='utf-8');print(report);print('GUI process exit:',result.returncode)
raise SystemExit(result.returncode)
