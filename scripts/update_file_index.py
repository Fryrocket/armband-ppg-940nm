#!/usr/bin/env python3
"""Single-repo File index updater. Used by the GitHub Action."""
from __future__ import annotations
import os, re, sys
from pathlib import Path

DESCRIPTIONS = {
    "firmware/Armband_Full.ino": "**main firmware** (INT1 wake, 940 nm EMA, MQTT, deep sleep)",
    "firmware/MAX30102_Full_Monitor.ino": "HR/SpO₂/temp + OLED bench sketch",
    "firmware/MAX30102_HeartRate_Temp_OLED.ino": "earlier HR/temp sketch",
    "PINOUT.md": "printable pinout + wire colour card",
    "SETUP.md": "hardware, libraries, config, first run",
    "NOTES.md": "development log and tuning notes",
}

GROUP_RULES = [
    ("Firmware", ["firmware/"]),
    ("Docs", ["docs/", "PINOUT.md", "SETUP.md", "NOTES.md", "HARDWARE.md"]),
    ("Scripts", ["scripts/"]),
    ("Build", ["platformio.ini"]),
    ("Config", ["config.example.yaml", "requirements.txt", "LICENSE", ".gitignore"]),
    ("Root", ["LICENSE", ".gitignore"]),
]

SKIP = {".git", ".DS_Store", "__pycache__", ".venv", "node_modules", ".pytest_cache", ".github"}

def list_files(repo: Path):
    out = []
    for root, dirs, names in os.walk(repo):
        dirs[:] = [d for d in dirs if d not in SKIP and not d.startswith(".")]
        for name in names:
            if name in SKIP or (name.startswith(".") and name != ".gitignore"):
                continue
            rel = os.path.relpath(os.path.join(root, name), repo).replace(os.sep, "/")
            if rel == "README.md":
                continue
            out.append(rel)
    return sorted(out)

def group(files):
    used, groups = set(), {}
    for heading, prefixes in GROUP_RULES:
        matched = []
        for f in files:
            if f in used: continue
            for p in prefixes:
                if f == p or f.startswith(p):
                    matched.append(f); used.add(f); break
        if matched:
            groups[heading] = matched
    rem = [f for f in files if f not in used]
    if rem: groups["Other"] = rem
    order = [h for h,_ in GROUP_RULES] + ["Other"]
    return [(h, sorted(groups[h])) for h in order if h in groups]

def render(groups):
    lines = ["## File index", ""]
    for heading, files in groups:
        lines.append(f"**{heading}**")
        for f in files:
            d = DESCRIPTIONS.get(f, "")
            lines.append(f"- [{f}]({f}) — {d}" if d else f"- [{f}]({f})")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"

def replace(text, block):
    pat = re.compile(r"(^## File index\s*\n)(.*?)(?=^## |\Z)", re.M | re.S)
    if pat.search(text):
        return pat.sub(block + "\n", text, count=1)
    lic = re.compile(r"(^## License.*)", re.M | re.S)
    m = lic.search(text)
    if m:
        return text[:m.start()] + block + "\n" + text[m.start():]
    return text.rstrip() + "\n\n" + block

def main():
    repo = Path(".").resolve()
    readme = repo / "README.md"
    if not readme.is_file():
        print("No README.md"); return 1
    files = list_files(repo)
    block = render(group(files))
    original = readme.read_text(encoding="utf-8")
    updated = replace(original, block)
    if updated == original:
        print("File index already up to date")
        return 0
    readme.write_text(updated, encoding="utf-8")
    print(f"Updated File index ({len(files)} files)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
