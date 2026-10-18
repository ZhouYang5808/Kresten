#!/usr/bin/env python3
"""Boot x86 kernel directly, start desktop, capture FULL serial log, screendump."""
import subprocess, time, os, socket, select

OUT = "/tmp/x86_vbe_test"
os.makedirs(OUT, exist_ok=True)
for f in ("/tmp/vbe_mon.sock", "/tmp/vbe_ser.out", "/tmp/vbe_ser.in"):
    if os.path.exists(f):
        os.unlink(f)
os.mkfifo("/tmp/vbe_ser.out")
os.mkfifo("/tmp/vbe_ser.in")

qemu = [
    "qemu-system-x86_64", "-machine", "pc",
    "-bios", "/usr/share/seabios/bios.bin",
    "-kernel", "/root/os/x86/dist/kernel.elf",
    "-vga", "std",
    "-display", "none",
    "-monitor", "unix:/tmp/vbe_mon.sock,server,nowait",
    "-serial", "pipe:/tmp/vbe_ser",
    "-no-reboot",
]
proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

t0 = time.time()
mon = None
while time.time() - t0 < 30:
    if os.path.exists("/tmp/vbe_mon.sock"):
        try:
            mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            mon.settimeout(0.5)
            mon.connect("/tmp/vbe_mon.sock")
            break
        except OSError:
            time.sleep(0.1)
    time.sleep(0.1)

ser_in = open("/tmp/vbe_ser.in", "wb", buffering=0)
ser_out = open("/tmp/vbe_ser.out", "rb", buffering=0)

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
ser_in.write(b"desktop\n")
log = read_for(7)
print("=== AFTER desktop ===")
print(log)
print("=== END ===")
hmp("screendump " + OUT + "/d1.ppm")
time.sleep(1)
hmp("screendump " + OUT + "/d2.ppm")

ser_out.close()
ser_in.close()
mon.close()
proc.terminate()
proc.wait()
