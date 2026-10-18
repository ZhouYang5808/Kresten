#!/usr/bin/env python3
"""gen_hello_pe.py <out.exe>
手工构造最小 x86-64 PE console 程序: 经 kernel32 导入
CreateFileA/WriteFile/CloseHandle/ExitProcess 把 "Hello from wine (LX)!"
写入 C:\hello_out.txt (再辅以控制台输出)。不依赖 mingw, 供 wine loader
(tiny ISO 方案) 加载测试。

布局 (PE32+):
  DOS(0x40) PE 头(0x1C0) 段表 -> SizeOfHeaders 0x200
  .text  file 0x200  RVA 0x1000  (代码)
  .rdata file 0x400  RVA 0x2000  (导入目录/INT/IAT/名称/字符串)
  .data  file 0xA00  RVA 0x3000  (written 槽, 可写)
"""
import struct
import sys

out = sys.argv[1]

MSG = b"Hello from wine (LX)!\n"
NAME = b"C:\\hello_out.txt\x00"
assert len(MSG) == 22

IMPORT_RVA = 0x2000
INT_RVA = 0x2028
IAT_RVA = 0x2050
K32_RVA = 0x2078
BYNAME = [0x2080, 0x2090, 0x20A0, 0x20B0]   # CreateFileA/WriteFile/CloseHandle/ExitProcess
MSG_RVA = 0x20C0
NAME_RVA = 0x20E0
WRT_RVA = 0x3000

# ---------------- 头部 ----------------
dos = b"MZ" + b"\x00" * 0x3A + struct.pack("<I", 0x80)

coff = struct.pack(
    "<HHIIIHH",
    0x8664, 3, 0, 0, 0, 0xF0, 0x22,
)

opt = struct.pack("<HBBIIIIIQIIHHHHHHIIIIHHQQQQII",
    0x20B, 1, 0,
    0x200, 0x600, 0,
    0x1000, 0x1000,
    0x140000000,
    0x1000, 0x200,
    6, 0, 0, 0, 3, 0,
    0,
    0x4000, 0x200,
    0,
    3, 0,
    0x100000, 0x1000, 0x100000, 0x1000,
    0, 16,
)
assert len(opt) == 0x70
dd = [b"\x00" * 8] * 16
dd[1] = struct.pack("<II", IMPORT_RVA, 0x40)
dd[12] = struct.pack("<II", IAT_RVA, 0x28)
opt += b"".join(dd)
assert len(opt) == 0xF0

def sect(name, vsize, vaddr, rsize, roff, chars):
    return name + struct.pack("<IIIIIIHHI",
        vsize, vaddr, rsize, roff, 0, 0, 0, 0, chars)

header = (dos + b"\x00" * (0x80 - 0x40) + b"PE\x00\x00" + coff + opt
          + sect(b".text\x00\x00\x00", 0x200, 0x1000, 0x200, 0x200, 0x60000020)
          + sect(b".rdata\x00\x00", 0x200, 0x2000, 0x600, 0x400, 0x40000040)
          + sect(b".data\x00\x00\x00", 0x100, 0x3000, 0x200, 0xA00, 0xC0000040))
assert len(header) <= 0x200
header += b"\x00" * (0x200 - len(header))

# ---------------- .text: 代码 (程序化发射, 位移自动计算) ----------------
code = bytearray()
def ins(b):
    code.extend(b)
    return len(code)
def lea64(reg48, disp_target):
    at = 0x1000 + len(code) + 7
    code.extend(reg48 + struct.pack("<i", disp_target - at))
def call_ptr(iat_off):
    at = 0x1000 + len(code) + 6
    code.extend(b"\xFF\x15" + struct.pack("<i", IAT_RVA + iat_off - at))
def mov_rsp_imm32(off, imm):
    code.extend(b"\x48\xC7\x44\x24" + bytes([off]) + struct.pack("<I", imm))

ins(b"\x48\x83\xEC\x38")                        # sub rsp,0x38
lea64(b"\x48\x8D\x0D", NAME_RVA)                # lea rcx,[name]
ins(b"\xBA\x00\x00\x00\x40")                    # mov edx,GENERIC_WRITE
ins(b"\x45\x31\xC0")                            # xor r8d,r8d
ins(b"\x45\x31\xC9")                            # xor r9d,r9d
mov_rsp_imm32(0x20, 2)                          # [rsp+0x20]=5th dwCreationDisposition=CREATE_ALWAYS
mov_rsp_imm32(0x28, 0x80)                       # [rsp+0x28]=6th dwFlagsAndAttributes=FILE_ATTRIBUTE_NORMAL
mov_rsp_imm32(0x30, 0)                          # [rsp+0x30]=7th hTemplateFile=NULL
mov_rsp_imm32(0x38, 0)                          # [rsp+0x38]=8th padding
call_ptr(0)                                     # call CreateFileA
ins(b"\x48\x89\xC3")                            # mov rbx,rax (hFile)
ins(b"\x48\x89\xD9")                            # mov rcx,rbx
lea64(b"\x48\x8D\x15", MSG_RVA)                 # lea rdx,[msg]
ins(b"\x41\xB8\x16\x00\x00\x00")                # mov r8d,22
lea64(b"\x4C\x8D\x0D", WRT_RVA)                 # lea r9,[written]
mov_rsp_imm32(0x20, 0)                          # lpOverlapped=NULL
call_ptr(8)                                     # call WriteFile
ins(b"\x48\x89\xD9")                            # mov rcx,rbx
call_ptr(16)                                    # call CloseHandle
ins(b"\x31\xC9")                                # xor ecx,ecx
call_ptr(24)                                    # call ExitProcess
ins(b"\xF4")                                    # hlt
assert len(code) <= 0x200
code += b"\x00" * (0x200 - len(code))

# ---------------- .rdata ----------------
by_name = lambda rva: struct.pack("<Q", rva)  # IMAGE_THUNK_DATA 是 8 字节 QWORD
imp_desc = struct.pack("<IIIII", INT_RVA, 0, 0, K32_RVA, IAT_RVA) + b"\x00" * 20
int_tbl = b"".join(by_name(x) for x in BYNAME) + b"\x00" * 8
iat_tbl = int_tbl

rdata = bytearray(b"\x00" * 0x600)
rdata[0x000:0x028] = imp_desc
rdata[0x028:0x050] = int_tbl
rdata[0x050:0x078] = iat_tbl
rdata[0x078:0x085] = b"kernel32.dll\x00"
rdata[0x080:0x090] = b"\x00\x00" + b"CreateFileA\x00"
rdata[0x090:0x09F] = b"\x00\x00" + b"WriteFile\x00"
rdata[0x0A0:0x0AD] = b"\x00\x00" + b"CloseHandle\x00"
rdata[0x0B0:0x0BD] = b"\x00\x00" + b"ExitProcess\x00"
rdata[0x0C0:0x0D6] = MSG
rdata[0x0E0:0x0F2] = NAME

data = bytearray(b"\x00" * 0x200)   # .data: written 槽 @RVA 0x3000

pe = header + bytes(code) + bytes(rdata) + bytes(data)
with open(out, "wb") as f:
    f.write(pe)
print("hello PE written: %s (%d bytes)" % (out, len(pe)))