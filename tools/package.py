"""Packs the built plugin: dist/OBSPETS-<version>-setup.exe and dist/OBSPETS-<version>-windows-x64.zip.

    python tools/package.py [--dll PATH] [--pdb PATH | --no-pdb] [--source-url URL] [--no-installer]
                            [--sign "signtool sign /fd sha256 ... $f"]

Build first (see README.md). The version comes from buildspec.json.

The setup .exe is what the website offers. It is tools/installer/obspet.iss, built with Inno Setup
6.7+ (winget install JRSoftware.InnoSetup). It copies obspet\\ to
%ProgramData%\\obs-studio\\plugins\\obspet, where OBS loads it, and adds OBSPETS to Windows' Installed
apps, which uninstalls it.

The zip is for installing by hand (its README.txt says how):

    README.txt, THIRD-PARTY.txt, LICENSE.txt, SOURCE.txt     (tools/zip/, LICENSE)
    obspet/obspet.dll  (+ obspet.pdb)                        the build
    obspet/data/...                                          data/: locale, the bundled packs

Only packs that may be shared belong in data/packs/: a pack that reaches outside its own folder
("../" paths), or one named in PRIVATE, stops the build. Stdlib only.
"""
import argparse
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN = os.path.normpath(os.path.join(HERE, ".."))
BUILD = os.path.join(PLUGIN, "build_x64")
DATA = os.path.join(PLUGIN, "data")
DIST_FILES = os.path.join(HERE, "zip")
INSTALLER = os.path.join(HERE, "installer", "obspet.iss")
SOURCE_URL = "https://github.com/Sindruid/OBSPETS"
PRIVATE = {"yuki"}             # licensed art and sounds: never in a build
SKIP = {"thumbs.db", "desktop.ini", ".ds_store"}

# Where a `cmake --build --preset windows-x64` leaves the dll (Visual Studio generator), newest wins
DLL_CANDIDATES = [
    os.path.join(BUILD, "RelWithDebInfo", "obspet.dll"),
    os.path.join(BUILD, "rundir", "RelWithDebInfo", "obspet.dll"),
    os.path.join(BUILD, "Release", "obspet.dll"),
    os.path.join(BUILD, "rundir", "Release", "obspet.dll"),
]


def die(msg):
    print("package.py: " + msg, file=sys.stderr)
    sys.exit(1)


def find_dll(given):
    if given:
        if not os.path.isfile(given):
            die(f"there's no dll at {given}")
        return os.path.abspath(given)
    found = [p for p in DLL_CANDIDATES if os.path.isfile(p)]
    if not found:
        die("obspet.dll isn't built yet. Looked in:\n  " + "\n  ".join(DLL_CANDIDATES)
            + "\nBuild it first (README.md: cmake --preset windows-x64, then cmake --build --preset windows-x64),"
            + " or pass --dll PATH.")
    return max(found, key=os.path.getmtime)


def check_dll(path):
    """It has to be a 64-bit (x64) Windows dll, or OBS won't load it."""
    with open(path, "rb") as f:
        head = f.read(4096)
    if head[:2] != b"MZ" or len(head) < 64:
        die(f"{path} isn't a Windows dll")
    pe = struct.unpack_from("<I", head, 0x3C)[0]
    if pe + 6 > len(head) or head[pe:pe + 4] != b"PE\0\0":
        die(f"{path} isn't a Windows dll")
    machine = struct.unpack_from("<H", head, pe + 4)[0]
    if machine != 0x8664:
        die(f"{path} isn't an x64 dll (machine 0x{machine:04x})")


def warn_if_stale(dll):
    src = [p for p in glob.glob(os.path.join(PLUGIN, "src", "**", "*"), recursive=True) if os.path.isfile(p)]
    newer = [p for p in src if os.path.getmtime(p) > os.path.getmtime(dll)]
    if newer:
        names = ", ".join(sorted(os.path.relpath(p, PLUGIN) for p in newer)[:5])
        print(f"warning: {len(newer)} source file(s) changed after the dll was built ({names}). Rebuild?")


def check_packs():
    """The bundled packs must be shareable and self-contained."""
    root = os.path.join(DATA, "packs")
    if not os.path.isdir(root):
        die(f"there's no {root}")
    names = []
    for name in sorted(os.listdir(root)):
        d = os.path.join(root, name)
        if not os.path.isdir(d):
            continue
        if name.lower() in PRIVATE:
            die(f"data/packs/{name} is licensed for one channel only and must not ship. Remove it from data/packs.")
        lua = os.path.join(d, "pack.lua")
        if not os.path.isfile(lua):
            continue
        text = open(lua, encoding="utf-8").read()
        code = re.sub(r"--[^\n]*", "", text)   # rough: ignore comments
        if re.search(r"""["'][^"'\n]*\.\.[/\\]""", code) or re.search(r"""["'](?:[A-Za-z]:[/\\]|/)""", code):
            die(f"data/packs/{name}/pack.lua points outside its folder: a bundled pack must hold its own files")
        if name[0] in "._":
            continue   # not listed by the plugin (the template's sheet.png is only an example)
        m = re.search(r"""\bsheet\s*=\s*["']([^"']+)["']""", code)
        if not m or not os.path.isfile(os.path.join(d, m.group(1))):
            die(f"data/packs/{name}: the sheet named in pack.lua isn't in the folder")
        names.append(name)
    if not names:
        die("data/packs has no character to ship")
    return names


def crlf(text):
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")


def find_iscc():
    """Inno Setup's compiler: $ISCC, then PATH, then where its installer puts it."""
    cands = [os.environ.get("ISCC"), shutil.which("ISCC")]
    if os.environ.get("LOCALAPPDATA"):
        cands.append(os.path.join(os.environ["LOCALAPPDATA"], "Programs", "Inno Setup 6", "ISCC.exe"))
    for var in ("ProgramFiles(x86)", "ProgramFiles"):
        if os.environ.get(var):
            cands.append(os.path.join(os.environ[var], "Inno Setup 6", "ISCC.exe"))
    return next((c for c in cands if c and os.path.isfile(c)), None)


def stage(root, dll, pdb, texts):
    """The files as they're installed: root/obspet/ (dll, pdb, data), the text files beside it."""
    if os.path.isdir(root):
        shutil.rmtree(root)
    dest = os.path.join(root, "obspet")
    os.makedirs(dest)
    shutil.copy2(dll, os.path.join(dest, "obspet.dll"))
    if pdb:
        shutil.copy2(pdb, os.path.join(dest, "obspet.pdb"))
    count = 0
    for base, dirs, files in os.walk(DATA):
        dirs[:] = sorted(d for d in dirs if not d.startswith(".") and d.lower() not in PRIVATE)
        for f in sorted(files):
            if f.lower() in SKIP or f.startswith("."):
                continue
            src = os.path.join(base, f)
            to = os.path.join(dest, "data", os.path.relpath(src, DATA))
            os.makedirs(os.path.dirname(to), exist_ok=True)
            shutil.copy2(src, to)
            count += 1
    for name, data in texts.items():
        with open(os.path.join(root, name), "wb") as f:
            f.write(data)
    return count


def write_zip(out, root, names):
    tmp = out + ".part"
    stamp = time.localtime()[:6]

    def add(z, path, arc):
        info = zipfile.ZipInfo(arc, stamp)
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        with open(path, "rb") as f:
            z.writestr(info, f.read())

    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for name in names:
            add(z, os.path.join(root, name), name)
        for base, dirs, files in os.walk(os.path.join(root, "obspet")):
            dirs.sort()
            for f in sorted(files):
                path = os.path.join(base, f)
                add(z, path, os.path.relpath(path, root).replace(os.sep, "/"))
    os.replace(tmp, out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dll", help="the built obspet.dll (default: the newest one in build_x64)")
    ap.add_argument("--pdb", help="its .pdb (default: next to the dll, if there)")
    ap.add_argument("--no-pdb", action="store_true", help="leave the debug symbols out")
    ap.add_argument("--source-url", default=SOURCE_URL,
                    help=f"where the plugin's source code is published (SOURCE.txt; GPL). Default: {SOURCE_URL}")
    ap.add_argument("--no-installer", action="store_true", help="build only the zip (no Inno Setup needed)")
    ap.add_argument("--sign", help="a command that signs file $f (Inno Setup SignTool syntax); "
                                   "signs Setup and its uninstaller")
    a = ap.parse_args()

    spec = json.load(open(os.path.join(PLUGIN, "buildspec.json"), encoding="utf-8"))
    version = spec["version"]
    dll = find_dll(a.dll)
    check_dll(dll)
    warn_if_stale(dll)
    pdb = None
    if not a.no_pdb:
        pdb = a.pdb or os.path.splitext(dll)[0] + ".pdb"
        if not os.path.isfile(pdb):
            if a.pdb:
                die(f"there's no pdb at {a.pdb}")
            pdb = None
    packs = check_packs()
    source = a.source_url
    iscc = None if a.no_installer else find_iscc()
    if not a.no_installer and not iscc:
        die("Inno Setup 6 isn't installed, so the setup .exe can't be built. Install it with\n"
            "  winget install JRSoftware.InnoSetup\n"
            "or set ISCC to its ISCC.exe, or pass --no-installer to build only the zip.")

    dist = os.path.join(PLUGIN, "dist")
    os.makedirs(dist, exist_ok=True)
    fill = lambda t: t.replace("{version}", version).replace("{source}", source)
    text = lambda name: crlf(fill(open(os.path.join(DIST_FILES, name), encoding="utf-8").read()))
    texts = {
        "README.txt": text("README.txt"),
        "THIRD-PARTY.txt": text("THIRD-PARTY.txt"),
        "SOURCE.txt": text("SOURCE.txt"),
        "LICENSE.txt": crlf(open(os.path.join(PLUGIN, "LICENSE"), encoding="utf-8").read()),
    }
    root = os.path.join(dist, "stage")
    count = stage(root, dll, pdb, texts)

    out = os.path.join(dist, f"OBSPETS-{version}-windows-x64.zip")
    write_zip(out, root, list(texts))
    print(f"{out}  ({os.path.getsize(out) / 1024:.0f} KB)")

    if iscc:
        exe = os.path.join(dist, f"OBSPETS-{version}-setup.exe")
        cmd = [iscc, "/Q", f"/DAppVersion={version}", f"/DStage={root}", f"/DOutDir={dist}"]
        if a.sign:
            cmd += ["/DSignCmd=1", f"/Sobspet={a.sign}"]
        r = subprocess.run(cmd + [INSTALLER])
        if r.returncode or not os.path.isfile(exe):
            die(f"Inno Setup failed (exit {r.returncode})")
        print(f"{exe}  ({os.path.getsize(exe) / 1024:.0f} KB" + (")" if a.sign else ", not signed)"))

    print(f"  dll: {dll}" + (f"\n  pdb: {pdb}" if pdb else "\n  pdb: (none)"))
    print(f"  data: {count} files; characters: {', '.join(packs)}")


if __name__ == "__main__":
    main()
