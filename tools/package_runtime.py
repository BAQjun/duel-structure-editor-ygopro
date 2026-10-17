from build_config import path_setting
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[1]
UP=ROOT/'upstream';OUT=path_setting('runtime_root','outputs/决构编辑器-v0.1.17');ASSETS=path_setting('asset_root')
OUT.mkdir(parents=True,exist_ok=True)
for folder in ('textures','script'):
 shutil.copytree(UP/folder,OUT/folder,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.git','premake5.lua'))
for name in ('strings.conf','lflist.conf','LICENSE'):
 shutil.copy2(UP/name,OUT/name)
shutil.copy2(ASSETS/'cards.cdb',OUT/'cards.cdb')
for folder in ('single','replay','deck','pics'): (OUT/folder).mkdir(exist_ok=True)
for code in (55144522,46986414,89631139,69358454,83764718,12580477,70342110,53129443):
 image=ASSETS/'pics'/(str(code)+'.jpg')
 if image.exists():shutil.copy2(image,OUT/'pics'/image.name)
(OUT/'system.conf').write_text('use_d3d = 0\nwindow_width = 1024\nwindow_height = 640\ntextfont = ./fonts/textFont.ttc 14\nnumfont = ./fonts/numFont.ttf\nauto_save_replay = 1\n',encoding='utf-8')
(OUT/'single'/'duel_editor.lua').write_text('''-- Fixed, reproducible local test scenario. No AI; one user controls both players.
Debug.SetAIName("Player 2")
Debug.ReloadFieldBegin(DUEL_ATTACK_FIRST_TURN, 5)
Debug.SetPlayerInfo(0,8000,0,1)
Debug.SetPlayerInfo(1,8000,0,1)
for player=0,1 do
 for i=1,40 do
  Debug.AddCard(89631139,player,player,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE)
 end
end
Debug.ReloadFieldEnd()
''',encoding='utf-8')
shutil.copy2(UP/'bin'/'release'/'YGOPro.exe',OUT/'YGOPro.exe')
if not (OUT/'card-images.conf').exists():
 (OUT/'card-images.conf').write_text('',encoding='utf-8')
for lib in ('libc++.dll','libunwind.dll'):
 src=ROOT/'tools'/'llvm-mingw-20260616-ucrt-x86_64'/'bin'/lib
 if src.exists():shutil.copy2(src,OUT/lib)
print('Runtime prepared:',str(OUT))

if not (OUT/'editor-data.conf').exists():
 (OUT/'editor-data.conf').write_text('userdata/saves\n',encoding='utf-8')
