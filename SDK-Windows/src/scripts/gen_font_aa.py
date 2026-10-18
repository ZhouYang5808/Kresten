#!/usr/bin/env python3
"""Generate 4-bit alpha AA font from Terminus 8x16 (font8x16.h) using PIL.
Method: 8x supersample, gaussian blur (sigma ~1.0 src px), 8x8 box downsample,
quantize to 4-bit alpha.  Writes font_aa8x16.h into the three arch trees.
Run inside WSL:  python3 /tmp/kresten/x86/scripts/gen_font_aa.py
"""
import re, os
from PIL import Image, ImageFilter

SRC = '/tmp/kresten/x86/fonts/font8x16.h'
OUTS = [
    '/tmp/kresten/x86/fonts/font_aa8x16.h',
    '/tmp/kresten/arm/fonts/font_aa8x16.h',
    '/tmp/kresten/riscv/fonts/font_aa8x16.h',
]

SS = 8
SIGMA = 0.15  # gaussian sigma in source-pixel units (blur applied at SS scale)
THRESH = 90   # blur value at/above which the pixel is full alpha (15) even
              # without core hit; keeps thin strokes from fading

def load_src(path):
    with open(path) as f:
        text = f.read()
    body = re.search(r'static const uint8_t vga_font8x16\[\d+\] = \{(.*?)\};', text, re.S)
    if not body:
        raise SystemExit('array not found')
    return [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body.group(1))]

def glyph_image(bits, ch):
    img = Image.new('L', (8, 16), 0)
    px = img.load()
    for r in range(16):
        for c in range(8):
            if bits[ch * 32 + r] & (0x80 >> c):
                px[c, r] = 255
    return img

def build_glyph(img):
    big = img.resize((8 * SS, 16 * SS), Image.NEAREST)
    big = big.filter(ImageFilter.GaussianBlur(SIGMA * SS))
    return big.resize((8, 16), Image.BOX)

def pack(img, bits, ch):
    b = []
    px = img.load()
    for r in range(16):
        for c in range(0, 8, 2):
            a0 = alpha_for(px[c, r], bits[ch * 32 + r] & (0x80 >> c))
            a1 = alpha_for(px[c + 1, r], bits[ch * 32 + r] & (0x80 >> (c + 1)))
            b.append((a0 << 4) | a1)
    return b

def alpha_for(v, core):
    """4-bit alpha: core (original 1-bit stroke) pixels stay full; the
    rest is the blurred coverage compressed into the 0..15 range."""
    if core:
        return 15
    return min(15, v * 15 // THRESH)

def ascii_preview(img):
    chars = ' .:-=+*#%@'
    lines = []
    px = img.load()
    for r in range(16):
        lines.append(''.join(chars[min(9, (px[c, r] * 9) // 255)] for c in range(8)))
    return '\n'.join(lines)

def main():
    nums = load_src(SRC)
    assert len(nums) == 8192, 'bad size %d' % len(nums)
    glyphs = []
    for ch in range(256):
        img = build_glyph(glyph_image(nums, ch))
        glyphs.append(pack(img, nums, ch))
        if ch in (ord('A'), ord('g'), ord('0'), ord('W'), ord('e')):
            print('=== 0x%02X ===' % ch)
            print(ascii_preview(img))
    hdr = []
    hdr.append('/* Anti-aliased 8x16 font (4-bit alpha, 64 bytes/glyph),')
    hdr.append(' * generated from Terminus 8x16 by gen_font_aa.py')
    hdr.append(' * (8x supersample + gaussian sigma=%s + box downsample,' % SIGMA)
    hdr.append(' * core stroke pixels forced to alpha 15).')
    hdr.append(' * Layout: glyph = 16 rows x 8 px; each byte holds two')
    hdr.append(' * pixels (high nibble = left pixel), alpha 0..15. */')
    hdr.append('static const uint8_t vga_font8x16_aa[16384] = {')
    for ch in range(256):
        b = glyphs[ch]
        if ch % 8 == 0:
            hdr.append('    /* 0x%02X */' % ch)
        hdr.append('    ' + ', '.join('0x%02X' % v for v in b) + (',' if ch < 255 else ''))
    hdr.append('};')
    text = '\n'.join(hdr) + '\n'
    for path in OUTS:
        with open(path, 'w') as f:
            f.write(text)
        print('wrote', path, os.path.getsize(path), 'bytes')

if __name__ == '__main__':
    main()