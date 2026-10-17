from pathlib import Path
import json, os
ROOT = Path(__file__).resolve().parents[1]
def setting(name, default=None):
    local = ROOT / 'build-config.local.json'
    config = json.loads(local.read_text(encoding='utf-8-sig')) if local.exists() else {}
    return os.environ.get('DUEL_EDITOR_' + name.upper()) or config.get(name) or default
def path_setting(name, default=None):
    value = setting(name, default)
    if not value:
        raise RuntimeError('Configure DUEL_EDITOR_' + name.upper() + ' or ' + name + ' in build-config.local.json')
    path = Path(os.path.expandvars(str(value))).expanduser()
    return path if path.is_absolute() else ROOT / path
def unity_data():
    value = setting('unity_data')
    if value: return path_setting('unity_data')
    hub = Path(os.environ.get('ProgramFiles', '')) / 'Unity/Hub/Editor'
    candidates = sorted(hub.glob('*/Editor/Data'), reverse=True)
    if candidates: return candidates[0]
    return path_setting('unity_data')
