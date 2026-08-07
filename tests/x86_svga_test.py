#!/usr/bin/env python3
"""Boot x86 kernel with QEMU VMware SVGA (vmsvga) to reproduce VMware display issue."""
import subprocess, time, os, socket, select

OUT = "/tmp/x86_svga_test"
os.makedirs(OUT, exist_ok=True)
for f in ("/tmp/svga_mon.sock", "/tmp/svga_ser.out", "/tmp/svga_ser.in"):
    if os.path.exists(f):
        os.unlink(f)
os.mkfifo("/tmp/svga_ser.out")
os.mkfifo("/tmp/svga_ser.in")

qemu = [
    "qemu-system-x86_64", "-machine", "pc",
    "-bios", "/usr/share/seabios/bios.bin",
    "-cdrom", "/root/os/dist/myos-x86.iso",
    "-boot", "d",
    "-vga", "vmware",
    "-display", "none",
    "-monitor", "unix:/tmp/svga_mon.sock,server,nowait",
    "-serial", "pipe:/tmp/svga_ser",
    "-no-reboot",
]
proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

t0 = time.time()
mon = None
while time.time() - t0 < 30:
    if os.path.exists("/tmp/svga_mon.sock"):
        try:
            mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            mon.settimeout(0.5)
            mon.connect("/tmp/svga_mon.sock")
            break
        except OSError:
            time.sleep(0.1)
    time.sleep(0.1)

ser_in = open("/tmp/svga_ser.in", "wb", buffering=0)
ser_out = open("/tmp/svga_ser.out", "rb", buffering=0)

def hmp(c, w=0.3):
    mon.sendall(c.encode() + b"\n")
    time.sleep(w)

def read_for(seconds):
    buf = b""
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([ser_out], [], [], 0.2)
        if r:
            d = os.read(ser_out.fileno(), 4096)
            if d:
                buf += d
    return buf.decode(errors="replace")

log = read_for(6)
print("=== BOOT LOG ===")
print(log)
print("=== END ===")
hmp("screendump " + OUT + "/boot.ppm")
time.sleep(1)
hmp("screendump " + OUT + "/boot2.ppm")
log = read_for(8)   # diag screen holds ~10s, then desktop starts
print("=== AFTER DIAG ===")
print(log)
print("=== END ===")
hmp("screendump " + OUT + "/diag.ppm")
time.sleep(1)
hmp("screendump " + OUT + "/diag2.ppm")
log = read_for(8)
print("=== AFTER DESKTOP ===")
print(log)
print("=== END ===")
hmp("screendump " + OUT + "/desktop.ppm")
time.sleep(1)
hmp("screendump " + OUT + "/desktop2.ppm")

ser_out.close()
ser_in.close()
mon.close()
proc.terminate()
proc.wait()
