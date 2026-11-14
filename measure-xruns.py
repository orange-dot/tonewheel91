#!/usr/bin/env python3
"""measure-xruns.py — does SCHED_FIFO cut ALSA xruns for the Mamut-analog live driver?

Runs patchlab through a real ALSA device under a reproducible CPU load, driven by one of the
pc4 live drum takes, once at normal priority and once at SCHED_FIFO, and prints each run's
xrun count. Tests the sycl-lab/spsc finding (the scheduler, not the buffer, causes ms stalls).

patchlab is an interactive TUI and refuses to run without a terminal, so it is launched on a
real pseudo-terminal (pty.fork) here; isatty() then passes and output is captured off the PTY.

Needs root TWICE (hence sudo): snd-virmidi (a virtual MIDI cable for aplaymidi -> patchlab's
raw-MIDI input) and chrt -f (real-time priority; ulimit -r is 0 for a normal user here).

    sudo python3 measure-xruns.py

Three runs: normal priority, external `chrt -f`, and patchlab's own `--rt=PRIO` (same code
path the --rt flag ships). DUR caps each run's playback so you need not sit through the whole
take; at normal priority ~14 xruns/s means 60 s is plenty to show the contrast.

Knobs (environment): PCM, PATCH, PERIOD, PERIODS, LOADERS, FIFO_PRIO, MID, DUR (seconds; 0 = full)
If PipeWire holds the PCM device you get "Device or resource busy"; free it as your normal
user first:  systemctl --user stop pipewire pipewire-pulse wireplumber
"""
import os
import pty
import re
import select
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PATCHLAB = os.path.join(HERE, "build", "patchlab")
BANK = os.path.join(HERE, "patches", "mamutanalog")
MID = os.environ.get("MID", "/data/work-base/workspace/systems/pc4-microkit-studio/.pc4ms/"
                     "session-store/workbench-session/generated-drum-midi-takes/"
                     "drum-live-1781191727613.generated-live.mid")
PCM = os.environ.get("PCM", "hw:1,0")
PATCH = os.environ.get("PATCH", "raster")
PERIOD = os.environ.get("PERIOD", "128")
PERIODS = os.environ.get("PERIODS", "3")
RATE = "48000"
LOADERS = int(os.environ.get("LOADERS", "4"))
FIFO_PRIO = os.environ.get("FIFO_PRIO", "80")
DUR = int(os.environ.get("DUR", "60"))  # cap each run's playback (seconds); 0 = whole take


def die(msg):
    print(msg)
    sys.exit(1)


def find_virmidi():
    subprocess.run(["modprobe", "snd-virmidi"], stderr=subprocess.DEVNULL)
    card = None
    with open("/proc/asound/cards") as f:
        for line in f:
            if "Virtual Raw MIDI" in line or "VirMIDI" in line:
                card = line.split()[0]
                break
    if card is None:
        die("snd-virmidi not available")
    out = subprocess.run(["aconnect", "-o"], capture_output=True, text=True).stdout
    port = None
    for line in out.splitlines():
        m = re.match(r"client (\d+):.*(Virtual Raw MIDI|VirMIDI)", line)
        if m:
            port = m.group(1) + ":0"
            break
    if port is None:
        die("could not find the virmidi sequencer port (check: aconnect -o)")
    return "hw:%s,0" % card, port


def start_load():
    return [subprocess.Popen(["awk", "BEGIN{while(1){}}"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for _ in range(LOADERS)]


def stop_load(hogs):
    for h in hogs:
        h.kill()
    for h in hogs:
        h.wait()


def run(label, prefix, rawmidi, vport, extra=None):
    print("== %s ==" % label)
    argv = prefix + [PATCHLAB, "--patch", PATCH, "-b", BANK, "-d", PCM, "-m", rawmidi,
                     "-r", RATE, "-p", PERIOD, "-n", PERIODS, "-g", "0.4"] + (extra or [])
    hogs = start_load()
    pid, fd = pty.fork()
    if pid == 0:  # child: the PTY is stdin/stdout/stderr, so isatty() is true
        try:
            os.execvp(argv[0], argv)
        except OSError:
            os._exit(127)
    buf = bytearray()

    def drain(timeout):
        end = time.time() + timeout
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                try:
                    chunk = os.read(fd, 65536)
                except OSError:
                    return False
                if not chunk:
                    return False
                buf.extend(chunk)
        return True

    drain(1.5)
    if os.waitpid(pid, os.WNOHANG)[0] != 0:  # exited early
        stop_load(hogs)
        print("   patchlab exited early (device busy?). Output:")
        print("   " + buf.decode("latin-1").replace("\n", "\n   ").strip())
        return False
    play = ["aplaymidi", "-p", vport, MID]           # real-time playback
    if DUR > 0:
        play = ["timeout", str(DUR)] + play          # cap it so runs stay short
    subprocess.run(play)
    drain(1.0)
    os.kill(pid, signal.SIGINT)
    drain(2.0)
    try:
        os.close(fd)
    except OSError:
        pass
    os.waitpid(pid, 0)
    stop_load(hogs)
    text = buf.decode("latin-1")
    hits = [ln.strip() for ln in text.splitlines() if re.search(r"xrun|stopped", ln, re.I)]
    for ln in hits:
        print("   " + ln)
    if not hits:
        print("   (no summary line; tail:) " + text[-200:].replace("\n", " "))
    return True


def main():
    if os.geteuid() != 0:
        die("run with sudo (needs snd-virmidi + chrt)")
    if not os.access(PATCHLAB, os.X_OK):
        die("build patchlab first: make -C %s build/patchlab" % HERE)
    if not os.path.isfile(MID):
        die("MIDI take not found: %s" % MID)
    rawmidi, vport = find_virmidi()
    print("   raw-MIDI device %s, aplaymidi port %s" % (rawmidi, vport))
    print("device %s, patch %s, period %s x %s, %d CPU hogs, take %s"
          % (PCM, PATCH, PERIOD, PERIODS, LOADERS, os.path.basename(MID)))
    if not run("normal priority", [], rawmidi, vport, extra=["--no-rt"]):
        return
    run("external chrt -f %s" % FIFO_PRIO, ["chrt", "-f", FIFO_PRIO], rawmidi, vport,
        extra=["--no-rt"])  # chrt provides FIFO; keep patchlab from also self-elevating
    run("patchlab --rt=%s (in-code, now the default)" % FIFO_PRIO, [], rawmidi, vport,
        extra=["--rt=%s" % FIFO_PRIO])
    print("== done: normal should show xruns; chrt and --rt should both be ~0 ==")


if __name__ == "__main__":
    main()
