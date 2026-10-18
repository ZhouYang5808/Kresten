#!/bin/bash
# gen_tiny_iso.sh: 从 dist/wine.cpio.gz 提取 wine 运行最小集, 打成 dist/wine-tiny.iso
# 并生成 plugins/runtime/wine_iso_map.h (真实路径 -> ISO 8.3 路径 映射表)
# 卷标: WINETINY; 布局: /WINE/*.SO, /LIB/*.SO
cd "$(dirname "$0")/.."
set -e
STAGE="build/tinyiso"
rm -rf "$STAGE"
mkdir -p "$STAGE/WINE" "$STAGE/LIB"

python3 - "$STAGE" <<'PYEOF'
import gzip, os, sys
stage = sys.argv[1]
d = gzip.decompress(open('dist/wine.cpio.gz', 'rb').read())

def walk():
    off = 0
    while off + 110 <= len(d):
        if d[off:off+6] != b'070701': break
        hdr = d[off:off+110]
        namesz = int(hdr[94:102], 16); size = int(hdr[54:62], 16); mode = int(hdr[14:22], 16)
        nm = d[off+110:off+110+namesz].rstrip(b'\0').decode('latin1')
        if nm == 'TRAILER!!!': break
        namepad = (4-(110+namesz)%4)%4
        dat = off+110+namesz+namepad
        t = (mode>>12)&0xF
        yield nm, size, t, d[dat:dat+size]
        off = dat + size + (4-size%4)%4

def short8(name):
    stem = name.upper().split('.')[0][:8]
    return stem + '.SO'

wine_files = {}
lib_files = {
    'ld-linux-x86-64.so.2': 'LD-LINUX.SO',
    'libc.so.6':            'LIBC.SO6',
    'libm.so.6':            'LIBM.SO6',
    'libpthread.so.0':      'PTHREAD.SO',
    'libdl.so.2':           'LIBDL.SO',
    'libgcc_s.so.1':        'LIBGCC.SO',
    'libunwind.so.8':       'LIBUNWIND.SO',
    'libunwind-x86_64.so.8':'UNWINDX64.SO',
}
for nm, size, t, data in walk():
    if t in (2, 4, 10):
        continue
    base = nm.rsplit('/', 1)[-1]
    if nm.startswith('/usr/lib/x86_64-linux-gnu/wine/x86_64-unix/'):
        wine_files[base] = data
    elif base in lib_files:
        out = os.path.join(stage, 'LIB', lib_files[base])
        if not os.path.exists(out):
            open(out, 'wb').write(data)
        continue
    if nm == '/usr/lib/x86_64-linux-gnu/wine/wine':
        open(os.path.join(stage, 'WINE', 'WINE'), 'wb').write(data)

for base, data in sorted(wine_files.items()):
    out = os.path.join(stage, 'WINE', short8(base))
    if os.path.exists(out):
        print('WARN: 8.3 collision:', base, '->', out)
    open(out, 'wb').write(data)

print('tiny files:', len(wine_files) + 1 + len(lib_files))
print('WINE size:', sum(os.path.getsize(os.path.join(stage,'WINE',f)) for f in os.listdir(stage+'/WINE')))
print('LIB size :', sum(os.path.getsize(os.path.join(stage,'LIB',f)) for f in os.listdir(stage+'/LIB')))
PYEOF

# PE dll 闭包 (console 应用仅需 kernel32/kernelbase/ntdll): 从宿主 wine 安装取
PE_SRC=/usr/lib/x86_64-linux-gnu/wine/x86_64-windows
cp -f "$PE_SRC/kernel32.dll"   "$STAGE/WINE/KERNEL32.DLL"
cp -f "$PE_SRC/kernelbase.dll" "$STAGE/WINE/KERNELBA.DLL"
cp -f "$PE_SRC/ntdll.dll"      "$STAGE/WINE/NTDLL.DLL"
# 测试程序 PE (gen_hello_pe.py 手工构造, 写 C:\hello_out.txt)
python3 scripts/gen_hello_pe.py "$STAGE/WINE/HELLO.EXE"
ls -l "$STAGE/WINE"

# 无 Rock Ridge (mkisofs 模拟默认 off), 纯 ISO9660, 记录紧凑不跨扇区
# liblzma.so.5: ntdll.so 的 NEEDED, cpio 里是符号链接无内容, 从宿主取真实文件
if [ -f /usr/lib/x86_64-linux-gnu/liblzma.so.5 ]; then
    cp -f /usr/lib/x86_64-linux-gnu/liblzma.so.5 "$STAGE/LIB/LIBLZMA.SO5"
fi
xorriso -as mkisofs -iso-level 3 -V WINETINY -o dist/wine-tiny.iso "$STAGE" >/dev/null 2>&1
echo "=== dist/wine-tiny.iso ==="
ls -l dist/wine-tiny.iso

# 解析 ISO9660 真实记录名, 生成映射表头
python3 - <<'PYEOF'
import struct
iso = open('dist/wine-tiny.iso','rb')
iso.seek(16*2048); pvd = iso.read(2048)
root = pvd[156:190]
lsn = struct.unpack('<I', root[2:6])[0]; size = struct.unpack('<I', root[10:14])[0]

def read_dir(lsn, size):
    iso.seek(lsn*2048); data = iso.read(((size+2047)//2048)*2048)
    out = []; off = 0
    while off + 34 <= size:
        l = data[off]
        if l == 0:
            off += 1; continue
        nl = data[off+32]
        nm = data[off+33:off+33+nl]
        if nm.endswith(b';1'): nm = nm[:-2]
        if nm.endswith(b'.'): nm = nm[:-1]
        if nm not in (b'\x00', b'\x01'):
            flags = data[off+25]
            el = struct.unpack('<I', data[off+2:off+6])[0]
            sl = struct.unpack('<I', data[off+10:off+14])[0]
            out.append((nm.decode('latin1'), flags, el, sl))
        off += l
    return out

def get_dir(lsn, size, name):
    for nm, f, el, sl in read_dir(lsn, size):
        if nm == name:
            return el, sl
    return None

# stage 短名 -> ISO 记录名
short2rec = {}
for dname in ('WINE', 'LIB'):
    el, sl = get_dir(lsn, size, dname)
    for nm, f, e2, s2 in read_dir(el, sl):
        short2rec[(dname, nm.upper().replace('-', '_'))] = nm

# 真实路径 -> ISO 路径
import os
pairs = []
stage = 'build/tinyiso'
pe_map = {
    'KERNEL32.DLL': '/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/kernel32.dll',
    'KERNELBA.DLL': '/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/kernelbase.dll',
    'NTDLL.DLL':    '/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/ntdll.dll',
    'HELLO.EXE':    '/winebin/hello.exe',
}
def walk_dir(d):
    for fn in os.listdir(os.path.join(stage, d)):
        if fn == 'WINE':
            pairs.append(('/usr/lib/x86_64-linux-gnu/wine/wine', '/' + d + '/' + short2rec[(d, 'WINE')]))
            continue
        if fn in pe_map:
            real = pe_map[fn]
        else:
            base = fn.split('.')[0]
            if d == 'WINE':
                real = '/usr/lib/x86_64-linux-gnu/wine/x86_64-unix/' + base.lower() + '.so'
            else:
                real_map = {
                    'LD-LINUX': '/lib64/ld-linux-x86-64.so.2',
                    'LIBC':     '/usr/lib/x86_64-linux-gnu/libc.so.6',
                    'LIBM':     '/usr/lib/x86_64-linux-gnu/libm.so.6',
                    'PTHREAD':  '/usr/lib/x86_64-linux-gnu/libpthread.so.0',
                    'LIBDL':    '/usr/lib/x86_64-linux-gnu/libdl.so.2',
                    'LIBGCC':   '/usr/lib/x86_64-linux-gnu/libgcc_s.so.1',
                    'LIBUNWIND':'/usr/lib/x86_64-linux-gnu/libunwind.so.8',
                    'UNWINDX64':'/usr/lib/x86_64-linux-gnu/libunwind-x86_64.so.8',
                    'LIBLZMA':  '/usr/lib/x86_64-linux-gnu/liblzma.so.5',
                }
                real = real_map[base]
        rec = short2rec.get((d, fn.upper().replace('-', '_')))
        if not rec:
            raise SystemExit('MISSING record for ' + fn)
        pairs.append((real, '/' + d + '/' + rec))

walk_dir('WINE')
walk_dir('LIB')
missing = [r for r, i in pairs if not i]
if missing:
    raise SystemExit('missing iso paths: %r' % missing)
pairs.sort()

hdr = 'plugins/runtime/wine_iso_map.h'
with open(hdr, 'w') as f:
    f.write('/* generated by scripts/gen_tiny_iso.sh - do not edit */\n')
    f.write('#ifndef WINE_ISO_MAP_H\n#define WINE_ISO_MAP_H\n')
    f.write('static const struct wine_iso_ent { const char *real; const char *iso; } wine_iso_map[] = {\n')
    for real, isop in pairs:
        f.write('    { "%s", "%s" },\n' % (real, isop))
    f.write('    { 0, 0 }\n};\n')
    f.write('#endif\n')
print('map entries:', len(pairs), '->', hdr)
PYEOF