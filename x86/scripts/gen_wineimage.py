#!/usr/bin/env python3
"""生成 wine 发行版镜像 dist/wine.cpio.gz (winefs 用)。

收集 x86_64-windows 全套 DLL + unix .so + 装载器 + ldd 递归依赖 +
/usr/share/wine 数据, 打成 cpio newc (绝对路径) 再 gzip level 9。

用法: 在 /tmp/kresten/x86 下运行: python3 scripts/gen_wineimage.py
"""
import os, subprocess, gzip, sys, glob

TOP = os.path.dirname(os.path.abspath(__file__)) + "/.."
OUT = os.path.join(TOP, "dist", "wine.cpio.gz")

def ldd_closure(roots):
    seen = set()
    todo = list(roots)
    while todo:
        p = todo.pop()
        if p in seen or not os.path.isfile(p):
            continue
        seen.add(p)
        try:
            out = subprocess.run(["ldd", p], capture_output=True, text=True).stdout
        except Exception:
            continue
        for line in out.splitlines():
            i = line.find("=> ")
            if i < 0:
                continue
            dep = line[i + 3:].split(" (")[0].strip()
            if dep and dep.startswith("/") and dep not in seen:
                todo.append(dep)
    return seen

def collect(minimal=False):
    files = set()          # real path -> cpio path
    wine_dir = "/usr/lib/x86_64-linux-gnu/wine"
    roots = []
    if minimal:
        sel = ["ntdll.dll", "kernelbase.dll", "kernel32.dll", "advapi32.dll",
               "user32.dll", "gdi32.dll", "shell32.dll"]
        wd = wine_dir + "/x86_64-windows"
        for s in sel:
            p = os.path.join(wd, s)
            if os.path.isfile(p):
                files.add((p, p))
        roots += sorted(glob.glob(wine_dir + "/x86_64-unix/*"))
        for r in roots:
            if os.path.isfile(r):
                files.add((r, r))
    else:
        for base in [wine_dir + "/x86_64-unix", wine_dir + "/x86_64-windows",
                     wine_dir + "/x86_64-windows/system32",
                     wine_dir + "/i386-windows"]:
            if os.path.isdir(base):
                roots += sorted(glob.glob(base + "/**", recursive=True))
        for r in roots:
            if os.path.isfile(r):
                files.add((r, r))
    for extra in ["/usr/lib/x86_64-linux-gnu/libwine.so.1"]:
        if os.path.isfile(extra):
            files.add((extra, extra))
    loader = "/usr/lib/x86_64-linux-gnu/wine/wine"
    if os.path.isfile(loader):
        files.add((loader, loader))
        ws = "/usr/lib/x86_64-linux-gnu/wine/wineserver"
        if os.path.isfile(ws):
            files.add((ws, ws))
        for dep in ldd_closure([loader, ws]):
            files.add((dep, dep))
        for ldso in glob.glob("/lib64/ld-linux*") + glob.glob("/lib/x86_64-linux-gnu/ld-linux*"):
            if os.path.isfile(ldso):
                files.add((ldso, ldso))
        ntdll = wine_dir + "/x86_64-unix/ntdll.so"
        if os.path.isfile(ntdll):
            for dep in ldd_closure([ntdll]):
                files.add((dep, dep))
    share = "/usr/share/wine"
    if os.path.isdir(share):
        for r in glob.glob(share + "/**", recursive=True):
            if os.path.isfile(r):
                files.add((r, r))
    bin_wine = glob.glob("/usr/bin/wine*")
    for b in bin_wine:
        if os.path.isfile(b) and not os.path.islink(b):
            files.add((b, b))
    for p in ["/usr/lib/x86_64-linux-gnu/libnss_files.so.2",
              "/usr/lib/x86_64-linux-gnu/libnss_dns.so.2",
              "/usr/lib/x86_64-linux-gnu/libnss_compat.so.2"]:
        if os.path.isfile(p):
            files.add((p, p))
    for p in ["/etc/nsswitch.conf", "/etc/passwd", "/etc/group",
              "/etc/hosts", "/etc/host.conf", "/etc/services",
              "/etc/resolv.conf"]:
        if os.path.isfile(p):
            files.add((p, p))
    dirs = set()
    for real, name in files:
        p = os.path.dirname(name)
        while p and p not in dirs:
            dirs.add(p)
            p = os.path.dirname(p)
    dirs.discard("/")
    return sorted(files), sorted(dirs)

def cpio_entry(out, name, mode, data):
    nb = name.encode()
    p = 110 + len(nb)
    padn = (4 - p % 4) % 4
    padd = (4 - len(data) % 4) % 4
    hdr = ("070701" + "%08X" % (len(out) + 1) + "%08X" % mode +
           "%08X%08X%08X%08X" % (0, 0, 0, 1) +
           "%08X%08X%08X%08X" % (len(data), 0, 0, 0) +
           "%08X%08X" % (0, len(nb)) +
           "%08X" % 0).encode()
    out += hdr + nb + b"\0" * padn + data + b"\0" * padd

def cpio_newc(items, dirs):
    out = bytearray()
    for name in dirs:
        cpio_entry(out, name, 0o040755, b"")
    for real, name in items:
        data = open(real, "rb").read()
        cpio_entry(out, name, 0o100644, data)
    tr = b"TRAILER!!!"
    p = 110 + len(tr)
    padn = (4 - p % 4) % 4
    hdr = ("070701" + "%08X" % (len(out) + 1) + "%08X" % 0o040755 +
           "%08X%08X%08X%08X" % (0, 0, 0, 1) +
           "%08X%08X%08X%08X" % (0, 0, 0, 0) +
           "%08X%08X" % (0, len(tr)) +
           "%08X" % 0).encode()
    out += hdr + tr + b"\0" * padn
    return bytes(out)

def main():
    minimal = "--minimal" in sys.argv
    items, dirs = collect(minimal)
    total = sum(os.path.getsize(r) for r, _ in items)
    print("files: %d  dirs: %d  total: %.1f MB" % (len(items), len(dirs), total / 1048576))
    cp = cpio_newc(items, dirs)
    print("cpio: %.1f MB" % (len(cp) / 1048576))
    gz = gzip.compress(cp, compresslevel=9)
    print("gzip: %.1f MB" % (len(gz) / 1048576))
    os.makedirs(os.path.join(TOP, "dist"), exist_ok=True)
    open(OUT, "wb").write(gz)
    print("wrote", OUT)

if __name__ == "__main__":
    main()