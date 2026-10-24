#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""install.py -- put the VFD-DOOM demo on a HiMedia HD900B over the LAN (Telnet + a temporary HTTP server on this PC).

    python3 tools/install.py --host <player-ip> --wad /path/to/your/DOOM.WAD
    python3 tools/install.py --host <player-ip> --wad-on-device /path/on/the/player/DOOM.WAD

NOTHING is written to the firmware flash, /system or /data: everything goes to /tmp (RAM) and is gone after a power cycle
(run this installer again afterwards).  The WAD is NOT part of this project: use your own copy of DOOM.WAD / DOOM1.WAD.
Python 3.8+, standard library only.
"""
import argparse
import hashlib
import http.server
import os
import shutil
import socket
import socketserver
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from hdtelnet import Device          # noqa: E402

BINARIES = ["fbdoom_vfd", "vfdmux", "vfdctl", "evlog"]
SCRIPTS = ["doomvfd.sh", "vfd_stop.sh", "find_keyboard.sh", "uninstall.sh"]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def local_ip_towards(host):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((host, 9))
        return s.getsockname()[0]
    finally:
        s.close()


class Handler(http.server.SimpleHTTPRequestHandler):
    serve_dir = "."

    def __init__(self, *a, **k):
        super().__init__(*a, directory=Handler.serve_dir, **k)

    def log_message(self, *a):
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True, help="IP address of the HD900B")
    ap.add_argument("--user", default="root"); ap.add_argument("--password", default="")
    ap.add_argument("--pc-ip", help="this PC's IP as seen from the player (auto-detected)")
    ap.add_argument("--port", type=int, default=8898, help="temporary HTTP server port on this PC (default 8898)")
    ap.add_argument("--dest", default="/tmp/vfd", help="directory on the player (default /tmp/vfd)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--wad", help="your own DOOM.WAD on this PC; it is copied to <dest>/DOOM.WAD (about 4-12 MB of RAM)")
    g.add_argument("--wad-on-device", help="path of a DOOM.WAD that already exists on the player; <dest>/DOOM.WAD is linked to it")
    ap.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    a = ap.parse_args()

    dist = os.path.join(ROOT, "dist")
    for n in BINARIES:
        if not os.path.isfile(os.path.join(dist, n)):
            sys.exit("dist/%s がありません。build/build.sh でビルドしてください。" % n)
    if a.wad and not os.path.isfile(a.wad):
        sys.exit("WAD が見つかりません: %s" % a.wad)

    pc_ip = a.pc_ip or local_ip_towards(a.host)
    print("== HD900B (%s) に接続します / PC は %s:%d" % (a.host, pc_ip, a.port))
    dev = Device(a.host, user=a.user, password=a.password)
    rc, out = dev.run("uname -a; ls /dev/venus_vfdo /dev/venus_vfd_wrctl 2>&1")
    print(out)
    if "2.6.34" not in out or "No such file" in out:
        print("!! 想定外の機種です(動作確認は HD900B / Linux 2.6.34-VENUS のみ。/dev/venus_vfdo が必要です)。")
        if not a.yes and input("続行しますか? [y/N] ").lower() != "y":
            sys.exit(1)
    elif not a.yes:
        print("このあと %s にファイルを置きます(フラッシュ・/system・/data は変更しません。電源を入れ直すと消えます)。" % a.dest)
        if input("続行しますか? [y/N] ").lower() != "y":
            sys.exit(1)

    tmp = tempfile.mkdtemp(prefix="vfddoom_")
    Handler.serve_dir = tmp
    for n in BINARIES:
        shutil.copy(os.path.join(dist, n), os.path.join(tmp, n))
    for n in SCRIPTS:
        with open(os.path.join(ROOT, "device", n), "rb") as f, open(os.path.join(tmp, n), "wb") as g_:
            g_.write(f.read().replace(b"\r\n", b"\n"))
    files = BINARIES + SCRIPTS
    if a.wad:
        shutil.copy(a.wad, os.path.join(tmp, "DOOM.WAD")); files.append("DOOM.WAD")

    socketserver.TCPServer.allow_reuse_address = True
    httpd = socketserver.ThreadingTCPServer(("0.0.0.0", a.port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    base = "http://%s:%d" % (pc_ip, a.port)
    try:
        dev.run("for p in $(pidof fbdoom_vfd vfdmux); do kill $p; done; mkdir -p %s" % a.dest)
        for n in files:
            rc, out = dev.run("wget -q -O %s/%s %s/%s" % (a.dest, n, base, n), timeout=300)
            if rc != 0:
                sys.exit("%s のダウンロードに失敗: %s" % (n, out))
        dev.run("cd %s && chmod +x %s" % (a.dest, " ".join(BINARIES + SCRIPTS)))
        rc, out = dev.run("cd %s && sha256sum %s" % (a.dest, " ".join(files)))
        have = {l.split()[1]: l.split()[0] for l in out.splitlines() if len(l.split()) == 2}
        for n in files:
            ok = have.get(n) == sha256(os.path.join(tmp, n))
            print("   %-18s %s" % (n, "OK" if ok else "MISMATCH"))
            if not ok:
                sys.exit("ハッシュが一致しません。中止します。")
        if a.wad_on_device:
            rc, out = dev.run("ln -sf %s %s/DOOM.WAD && [ -f %s/DOOM.WAD ]" % (a.wad_on_device, a.dest, a.dest))
            print("   DOOM.WAD ->", a.wad_on_device, "(OK)" if rc == 0 else "(失敗: %s)" % out)
    finally:
        httpd.shutdown()
        dev.close()
        shutil.rmtree(tmp, ignore_errors=True)
    print("\n完了。Telnet でプレーヤーに入り、次を実行してください:")
    print("  cd %s && ./doomvfd.sh" % a.dest)
    if not (a.wad or a.wad_on_device):
        print("  (DOOM.WAD を %s に置いてください。自分で用意した WAD を使います)" % a.dest)
    print("キーボードの event 番号が違う場合: ./find_keyboard.sh で確認し、EVDEV=/dev/input/eventN ./doomvfd.sh")
    print("元に戻す: %s/uninstall.sh   (表示は HD900B に戻り、%s が消えます)" % (a.dest, a.dest))


if __name__ == "__main__":
    main()
