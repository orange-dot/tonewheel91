#!/bin/sh
# Operator-run parity of the Workbench against the 2026-11-07 Hurt orchestra
# (plan §9: orchestration parity and render parity). Needs the gitignored
# sources in notes-midi/local/; skips when they are absent.
#
#   workbench/test/parity_hurt.sh            derived MIDI bytes + 30 s render prefix
#   workbench/test/parity_hurt.sh --full     plus both full renders (~10 min each)
set -eu

FULL=false
[ "${1:-}" = "--full" ] && FULL=true
SOURCE=notes-midi/local/hurt-songparts-vocals.mid
ORCH1=notes-midi/local/hurt-orch-20261107.mid
ORCH2=notes-midi/local/hurt-orch-20261107-v2.mid
for f in "$SOURCE" "$ORCH1" "$ORCH2"; do
    if [ ! -f "$f" ]; then echo "parity_hurt: skipped ($f absent)"; exit 0; fi
done
BUILD=${BUILD:-build}
WB=$BUILD/wb
EXPORT=$BUILD/export_hurt_orch
EXHIBIT=$BUILD/wb-parity/exhibit_ma3_hurt_orch
OUT=$BUILD/wb-parity
mkdir -p "$OUT"

# Logged in renders/mamut-analog/ma3_hurt_orch_20261107.md.
MIDI_SHA_1=3a16982b67ed00732cb93231b080bf2848d6b509fbcac6576efd70919ed388ee
MIDI_SHA_2=d4f7691cb47ed9693891fa7eb274057f843afe5f815fa08acb00866c44903e13
MIX_FNV_1=bac34975b9f912ec
MIX_FNV_2=96d601778ccf8305
STEMS_FNV_1="tonal=72fd8a25f427a10d bass=ad7d3dc4d01a7365 pads=13662462189da72c vocal=f33d89c07de85603 lead=877346e6304d9842 rhythm=6b4bf296c52eec12"

fail() { echo "parity_hurt: FAIL: $*" >&2; exit 1; }

for v in 1 2; do
    S=$OUT/hurt-v$v
    mkdir -p "$S/sources" "$S/patches"
    cp workbench/test/fixtures/hurt-v$v.mws "$S/hurt.mws"
    [ -f "$S/sources/hurt-songparts-vocals.mid" ] || cp "$SOURCE" "$S/sources/"
    "$EXPORT" $v "$S/patches" >/dev/null
    "$WB" check "$S/hurt.mws" >/dev/null || fail "v$v session check"
    sha=$("$WB" build-midi "$S/hurt.mws" -o "$OUT/hurt-v$v-derived.mid" | sed -n '1s/.*sha256 //p')
    eval want=\$MIDI_SHA_$v
    [ "$sha" = "$want" ] || fail "v$v derived MIDI $sha, want $want"
    echo "parity_hurt: v$v derived MIDI byte-identical ($sha)"

    # 30 s prefix against the exhibit built from this tree.
    "$EXHIBIT" --edition $v --render "$OUT/ex$v-30" --seconds 30 >"$OUT/ex$v-30.log"
    "$WB" render "$S/hurt.mws" --seconds 30 --label parity30 --no-commit >"$OUT/wb$v-30.log"
    for stem in tonal bass pads vocal lead rhythm mix; do
        a=$(sed -n "s/.*ex$v-30_$stem.wav.*FNV64=\([0-9a-f]*\).*/\1/p" "$OUT/ex$v-30.log")
        b=$(sed -n "s/^  $stem *FNV64=\([0-9a-f]*\).*/\1/p" "$OUT/wb$v-30.log")
        [ -n "$a" ] && [ "$a" = "$b" ] || fail "v$v 30 s $stem: exhibit $a, wb $b"
    done
    echo "parity_hurt: v$v 30 s prefix: 7/7 outputs equal to the exhibit"

    if $FULL; then
        "$WB" render "$S/hurt.mws" --label parity-full --no-commit >"$OUT/wb$v-full.log"
        mix=$(sed -n 's/^  mix *FNV64=\([0-9a-f]*\).*/\1/p' "$OUT/wb$v-full.log")
        eval want=\$MIX_FNV_$v
        [ "$mix" = "$want" ] || fail "v$v full mix $mix, want $want"
        if [ $v = 1 ]; then
            for pair in $STEMS_FNV_1; do
                stem=${pair%%=*}; want=${pair#*=}
                got=$(sed -n "s/^  $stem *FNV64=\([0-9a-f]*\).*/\1/p" "$OUT/wb$v-full.log")
                [ "$got" = "$want" ] || fail "v1 full $stem $got, want $want"
            done
        fi
        echo "parity_hurt: v$v full render mix $mix as logged"
    fi
done
echo "parity_hurt: ok"
