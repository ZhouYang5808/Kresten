#!/usr/bin/env python3
"""Generate plugins/lx_tests.h from a set of ELF test programs.

Usage: gen_tests.py <prog.elf> [<prog2.elf> ...] <out_header>
Program names are taken from file basenames (e.g. hello.elf -> "hello").
"""
import os
import sys


def to_c_array(data, name):
    lines = []
    for i in range(0, len(data), 16):
        chunk = ", ".join("0x%02X" % b for b in data[i:i + 16])
        lines.append("    " + chunk + ",")
    return (
        "static const uint8_t %s[] = {\n%s\n};\n" % (name, "\n".join(lines))
    )


def main():
    if len(sys.argv) < 3:
        print("usage: gen_tests.py <prog.elf> [...] <out_header>")
        return 1
    out_header = sys.argv[-1]
    elf_files = sys.argv[1:-1]
    if not elf_files:
        print("no ELF inputs")
        return 1

    out = []
    out.append("#ifndef _LX_TESTS_H")
    out.append("#define _LX_TESTS_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("#include \"lx_int.h\"")
    out.append("")
    for path in elf_files:
        data = open(path, "rb").read()
        base = os.path.basename(path)
        name, _ = os.path.splitext(base)
        var = "lx_" + name + "_elf"
        print("%s: %d bytes" % (base, len(data)))
        out.append(to_c_array(data, var))
        out.append("")
    out.append("static const LxTest lx_tests[] = {")
    for path in elf_files:
        base = os.path.basename(path)
        name, _ = os.path.splitext(base)
        out.append("    {\"%s\", lx_%s_elf, sizeof(lx_%s_elf)}," % (name, name, name))
    out.append("    {0, 0, 0}")
    out.append("};")
    out.append("")
    out.append("#endif")
    open(out_header, "w").write("\n".join(out) + "\n")
    print("wrote %s" % out_header)
    return 0


if __name__ == "__main__":
    sys.exit(main())
