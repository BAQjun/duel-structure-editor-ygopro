from pathlib import Path
import json, hashlib, zipfile
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'outputs'; RUN=OUT/'决构编辑器-v0.1.16'; UP=ROOT/'upstream'
assert RUN.is_dir()
SOURCE=OUT/'source-v0.1.16.zip'; PORTABLE=OUT/'duel-editor-v0.1.16.zip'
reports=['使用说明.md','v0.1.16-召唤素材说明.md','引擎验证.txt','YGO-召唤素材验证.txt','视角与独立时点验证.txt','卡片状态审计.md','源码基线.json']
with zipfile.ZipFile(SOURCE,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for folder in ['gframe','ocgcore','premake','resource']:
  for p in (UP/folder).rglob('*'):
   if p.is_file() and '.git' not in p.parts:z.write(p,'upstream/'+p.relative_to(UP).as_posix())
 for p in UP.iterdir():
  if p.is_file() and p.name!='premake5.exe':z.write(p,'upstream/'+p.name)
 for folder in ['tools','tests']:
  for p in (ROOT/folder).rglob('*'):
   if p.is_file() and p.parent==ROOT/folder and p.suffix in ['.py','.ps1','.cpp','.h']:z.write(p,p.relative_to(ROOT).as_posix())
 z.write(ROOT/'README.md','README.md')
 for name in reports:z.write(OUT/name,'outputs/'+name)
 for name in ['summon-materials-preview.png']:z.write(OUT/name,'outputs/'+name)
 z.write(ROOT/'work/dependency-sources.json','work/dependency-sources.json')
 for name in ['single/duel_editor.lua','deck/示例测试.ydk']:z.write(RUN/name,'outputs/'+RUN.name+'/'+name)
current=(RUN/'.card-updates/current.txt').read_text(encoding='utf-8').strip()
examples={'editor-engine-smoke.yrp','editor-deck-smoke.yrp','v015-attachments.yrp','v015-native-gui.yrp'}
with zipfile.ZipFile(PORTABLE,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for p in RUN.rglob('*'):
  if not p.is_file():continue
  rel=p.relative_to(RUN);parts=rel.parts
  if 'saves' in parts or p.suffix=='.log':continue
  if parts[0]=='replay' and p.name not in examples:continue
  if parts[0]=='.card-updates':
   if len(parts)>1 and parts[1]=='versions':
    if len(parts)<3 or parts[2]!=current:continue
   elif rel.as_posix()!='.card-updates/current.txt':continue
  z.write(p,RUN.name+'/'+rel.as_posix())
 for name in reports:z.write(OUT/name,'验证与说明/'+name)
for archive in [SOURCE,PORTABLE]:
 with zipfile.ZipFile(archive) as z:
  bad=z.testzip()
  if bad:raise RuntimeError('Damaged archive member: '+bad)
print(json.dumps({p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [SOURCE,PORTABLE]},ensure_ascii=False,indent=2))
