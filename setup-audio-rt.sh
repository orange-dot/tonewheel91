#!/usr/bin/env bash
# setup-audio-rt.sh — let a normal user get SCHED_FIFO + mlockall without sudo, for patchlab --rt.
#
# Puts the user in the 'audio' group and raises that group's RLIMIT_RTPRIO and RLIMIT_MEMLOCK
# through /etc/security/limits.d, which PAM applies at login. After this and a re-login, running
# patchlab engages real-time on its own (patchlab --rt is the live-mode default) — no sudo, no chrt.
#
# This is why the right value is a group limit, not a one-off: sched_setscheduler(SCHED_FIFO)
# checks RLIMIT_RTPRIO, and mlockall checks RLIMIT_MEMLOCK. ulimit -r is 0 for a normal user by
# default (distros gate it so a runaway RT thread cannot freeze the machine); @audio lifts it.
#
# Run:  sudo bash setup-audio-rt.sh                 # sets up whoever invoked sudo
#       sudo AUDIO_USER=name bash setup-audio-rt.sh # or name the user
# Knobs: GROUP (default audio), RTPRIO (95), MEMLOCK (unlimited), NICE (-19)
set -eu

GROUP=${GROUP:-audio}
RTPRIO=${RTPRIO:-95}       # patchlab asks for 80, so 95 leaves headroom and stays below kernel threads
MEMLOCK=${MEMLOCK:-unlimited}
NICE=${NICE:--19}
CONF=/etc/security/limits.d/95-audio-rt.conf
USER_TO_ADD=${AUDIO_USER:-${SUDO_USER:-}}

[ "$(id -u)" = 0 ] || { echo "run with sudo (edits /etc/security/limits.d and group membership)"; exit 1; }
[ -n "$USER_TO_ADD" ] || { echo "cannot tell which user to set up; pass AUDIO_USER=<name>"; exit 1; }
id "$USER_TO_ADD" >/dev/null 2>&1 || { echo "no such user: $USER_TO_ADD"; exit 1; }
[ "$USER_TO_ADD" != root ] || { echo "target resolved to root (you are in a root shell, so SUDO_USER=root)."; \
    echo "root ignores the rtprio limit anyway. Pass your login instead, e.g.  sudo AUDIO_USER=dev bash setup-audio-rt.sh"; exit 1; }

# 1. the group
if getent group "$GROUP" >/dev/null; then
    echo "group $GROUP exists"
else
    echo "creating group $GROUP"; groupadd "$GROUP"
fi

# 2. membership
if id -nG "$USER_TO_ADD" | tr ' ' '\n' | grep -qx "$GROUP"; then
    echo "$USER_TO_ADD is already in $GROUP"
else
    echo "adding $USER_TO_ADD to $GROUP"; usermod -aG "$GROUP" "$USER_TO_ADD"
fi

# 3. the limits (overwrite our own managed file)
echo "writing $CONF"
cat > "$CONF" <<EOF
# Managed by tonewheel91/setup-audio-rt.sh — real-time audio limits for @$GROUP.
# rtprio: max SCHED_FIFO/RR priority; memlock: lockable memory (mlockall); nice: min niceness.
@$GROUP   -   rtprio    $RTPRIO
@$GROUP   -   memlock   $MEMLOCK
@$GROUP   -   nice      $NICE
EOF
chmod 644 "$CONF"

cat <<EOF

done: $USER_TO_ADD is in @$GROUP, and @$GROUP now has rtprio $RTPRIO, memlock $MEMLOCK, nice $NICE.

Log out and back in (a full session logout, or reboot) for it to take effect — PAM applies
limits at login, and the current shell / session will NOT show them. Then, as $USER_TO_ADD:
    ulimit -r      # expect $RTPRIO (was 0)
    ulimit -l      # expect $MEMLOCK
    id -nG | tr ' ' '\\n' | grep $GROUP

After that patchlab goes real-time with no sudo:
    ./build/patchlab --patch raster -d hw:1,0 -m <rawmidi>     # --rt is the default; --no-rt opts out

To undo: remove $CONF and run  sudo gpasswd -d $USER_TO_ADD $GROUP  then re-login.
EOF
