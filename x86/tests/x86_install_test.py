#!/usr/bin/env python3
"""Full install flow test: boot installer ISO, install to empty disk,
then reboot from the hard disk and verify the desktop starts."""
import subprocess, time, os, socket, select

OUT = "/tmp/x86_install_test"
os.makedirs(OUT, exist_ok=True)
DISK = "/tmp/x86_install_disk.img"
if os.path.exists(DISK):
    os.unlink(DISK)
with open(DISK, "wb") as f:
    f.truncate(64 * 1024 * 1024)

def mkfifos(tag):
    for f in ("/tmp/%s_mon.sock" % tag, "/tmp/%s_ser.out" % tag, "/tmp/%s_ser.in" % tag):
        if os.path.exists(f):
            os.unlink(f)
    os.mkfifo("/tmp/%s_ser.out" % tag)
    os.mkfifo("/tmp/%s_ser.in" % tag)

def run_qemu(tag, extra, boot, no_reboot=True):
    mkfifos(tag)
    qemu = [
        "qemu-system-x86_64", "-machine", "pc",
        "-bios", "/usr/share/seabios/bios.bin",
        "-hda", DISK,
        "-boot", boot,
        "-vga", "vmware",
        "-display", "none",
        "-monitor", "unix:/tmp/%s_mon.sock,server,nowait" % tag,
        "-serial", "pipe:/tmp/%s_ser" % tag,
    ]
    if no_reboot:
        qemu.append("-no-reboot")
    qemu += extra
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    mon = None
    while time.time() - t0 < 30:
        if os.path.exists("/tmp/%s_mon.sock" % tag):
            try:
                mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                mon.settimeout(0.5)
                mon.connect("/tmp/%s_mon.sock" % tag)
                break
            except OSError:
                time.sleep(0.1)
        time.sleep(0.1)
    ser_in = open("/tmp/%s_ser.in" % tag, "wb", buffering=0)
    ser_out = open("/tmp/%s_ser.out" % tag, "rb", buffering=0)
    return proc, mon, ser_in, ser_out

def read_for(ser_out, seconds):
    buf = b""
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([ser_out], [], [], 0.2)
        if r:
            d = os.read(ser_out.fileno(), 4096)
            if d:
                buf += d
    return buf.decode(errors="replace")

print("=== PHASE 1: INSTALL + AUTO-REBOOT ===")
proc, mon, ser_in, ser_out = run_qemu("inst", ["-cdrom", "/root/os/dist/kresten-x86-install.iso"], "once=d,order=c", no_reboot=False)
log = read_for(ser_out, 45)
print(log)
print("=== PHASE 1 END ===")
proc.terminate()
proc.wait()
ser_out.close()
ser_in.close()
mon.close()

ok = "INSTALL DONE" in log and "install complete" in log
print("INSTALL OK:", ok)
if not ok:
    raise SystemExit(1)
ok_reboot = "tabbed desktop started" in log
print("AUTO-REBOOT TO HDD OK:", ok_reboot)
if not ok_reboot:
    raise SystemExit(1)

print("=== PHASE 2: BOOT FROM HDD ===")
proc, mon, ser_in, ser_out = run_qemu("hdd", [], "c")
log = read_for(ser_out, 25)
print(log)
print("=== PHASE 2 END ===")
proc.terminate()
proc.wait()
ser_out.close()
ser_in.close()
mon.close()

ok2 = "tabbed desktop started" in log
print("HDD BOOT OK:", ok2)
if not ok2:
    raise SystemExit(1)
