"""Fix 4 helper: adds the instant Google Sheet link to the board's secrets.h and lets the laptop website
forward face recognitions. Uses the addresses Setup already saved. Backs everything up first."""
import json, re, shutil, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import setup_stage3 as S

st = S.load_state()
url, key = st.get("sheet_url"), st.get("hook")
if not url or not key:
    print("   Setup is not finished (no Google Sheet address yet). Run 'Setup Stage 3.bat' first, then Apply Fix 4 again.")
    sys.exit(1)
hook = url + "?key=" + key
sec_p = S.BOARD_SECRETS
sec = sec_p.read_text(encoding="utf-8")
bak = S.ROOT / "_backup_before_fix4" / "secrets.h"
bak.parent.mkdir(exist_ok=True)
if not bak.exists():
    shutil.copy2(sec_p, bak)
if re.search(r'#define\s+SHEET_HOOK\s+"', sec):
    sec = re.sub(r'(#define\s+SHEET_HOOK\s+)"[^"]*"', lambda m: m.group(1) + json.dumps(hook), sec)
else:
    sec = sec.rstrip() + "\n#define SHEET_HOOK       " + json.dumps(hook) + "   // Google Sheet: instant rows + e-mail (added by Fix 4)\n"
sec_p.write_text(sec, encoding="utf-8")
print("   OK: secrets.h now has the instant Google Sheet link (your other keys are unchanged).")
for f in S.find_laptop_sites():
    stop = f.parent.parent / "tools" / "stop.ps1"
    if S.WINDOWS and stop.exists():
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(stop)])
    b2 = f.with_name("settings_backup_before_fix4.json")
    if not b2.exists():
        shutil.copy2(f, b2)
    cfg = S.jload(f.read_text(encoding="utf-8"))
    cfg.update({"webhook_url": hook, "webhook_events": "all"})
    f.write_text(json.dumps(cfg, indent=1), encoding="utf-8")
    print(f"   OK: laptop website at {f.parent.parent} now forwards face recognitions (the Sheet filters out the unimportant ones).")
