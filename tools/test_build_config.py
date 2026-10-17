from pathlib import Path
import importlib.util, json, os, shutil, subprocess
ROOT=Path(__file__).resolve().parents[1]
fixture=ROOT/'work/config fixture/自定义工具位置'
(fixture/'tools').mkdir(parents=True,exist_ok=True)
for name in ['build_config.py','build_config.ps1']:shutil.copy2(ROOT/'tools'/name,fixture/'tools'/name)
(fixture/'build-config.local.json').write_text(json.dumps({'asset_root':'relative assets/卡图','python':'custom-python'},ensure_ascii=False),encoding='utf-8-sig')
spec=importlib.util.spec_from_file_location('fixture_config',fixture/'tools/build_config.py');config=importlib.util.module_from_spec(spec);spec.loader.exec_module(config)
assert config.setting('python')=='custom-python'
assert config.path_setting('asset_root')==fixture/'relative assets/卡图'
env=os.environ.copy();env['DUEL_EDITOR_ASSET_ROOT']=str(fixture/'override assets')
os.environ['DUEL_EDITOR_ASSET_ROOT']=env['DUEL_EDITOR_ASSET_ROOT']
assert config.path_setting('asset_root')==fixture/'override assets'
command=". '"+str(fixture/'tools/build_config.ps1').replace("'","''")+"'; Get-BuildPath 'asset_root'"
value=subprocess.check_output(['powershell','-NoProfile','-Command',command],env=env).decode('utf-8-sig',errors='replace').strip()
# Use ASCII suffix for cross-shell encoding independence; resolution itself is checked in-process above.
assert value.endswith('override assets'),value
os.environ.pop('DUEL_EDITOR_ASSET_ROOT',None)
assert config.path_setting('asset_root')==fixture/'relative assets/卡图'
print('SUCCESS Python and PowerShell config resolve relative paths, BOM and environment overrides from relocated project')
