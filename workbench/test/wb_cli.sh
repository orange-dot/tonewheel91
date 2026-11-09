#!/bin/sh
# End-to-end test of the wb CLI against a scratch session root.
# Needs git. Never touches the real WB_HOME.
set -eu

WB=${WB:?set WB to the wb binary}
case $WB in /*) ;; *) WB=$PWD/$WB ;; esac
FIXTURE=$PWD/workbench/test/fixtures/hurt-v2.mws
ROOT=$(mktemp -d "${TMPDIR:-/tmp}/wb-cli-XXXXXX")
trap 'chmod -R u+w "$ROOT" 2>/dev/null; rm -rf "$ROOT"' EXIT
export WB_HOME="$ROOT/sessions"
# Commits in the scratch root need an identity even on a bare CI host.
export GIT_AUTHOR_NAME=wb-test GIT_AUTHOR_EMAIL=wb-test@localhost
export GIT_COMMITTER_NAME=wb-test GIT_COMMITTER_EMAIL=wb-test@localhost

fail() { echo "wb_cli: $*" >&2; exit 1; }
expect_fail() { if "$@" >/dev/null 2>&1; then fail "expected failure: $*"; fi; }

"$WB" root-init >/dev/null
"$WB" root-init >/dev/null                       # idempotent
test -f "$WB_HOME/README.txt" && test -f "$WB_HOME/.gitignore" || fail "root files"
test "$(git -C "$WB_HOME" rev-list --count HEAD)" = 1 || fail "one root commit"

S=$("$WB" new demo)
test "$S" = "$WB_HOME/demo/demo.mws" || fail "new printed $S"
for d in sources patches takes derived cache runs trash; do
    test -d "$WB_HOME/demo/$d" || fail "missing $d/"
done
expect_fail "$WB" new demo                        # refuses an existing session
expect_fail "$WB" check "$S"                      # skeleton is not complete yet

# A synthetic one-track SMF as the source.
printf 'MThd\000\000\000\006\000\000\000\001\001\340MTrk\000\000\000\004\000\377\057\000' >"$ROOT/in.mid"
"$WB" add-source "$S" src "$ROOT/in.mid" >/dev/null
test ! -w "$WB_HOME/demo/sources/in.mid" || fail "source copy must be read-only"
"$WB" add "$S" stem '[stem]
id=mix' >/dev/null 2>&1
"$WB" add "$S" part '[part]
id=lead
patch=patches/lead.mapatch
stem=mix' >/dev/null 2>&1
"$WB" set "$S" form end_bar 16 >/dev/null 2>&1
"$WB" check "$S" --no-files >/dev/null || fail "complete session must pass --no-files"
expect_fail "$WB" check "$S"                      # the patch file is missing

test "$("$WB" get "$S" part:lead gain)" = 1 || fail "default gain"
"$WB" set "$S" part:lead gain 1.25 >/dev/null 2>&1
test "$("$WB" get "$S" part:lead gain)" = 1.25 || fail "set gain"
expect_fail "$WB" set "$S" part:lead gain 99
expect_fail "$WB" set "$S" part:nobody gain 1
"$WB" undo "$S" >/dev/null 2>&1
test "$("$WB" get "$S" part:lead gain)" = 1 || fail "undo gain"
"$WB" redo "$S" >/dev/null 2>&1
test "$("$WB" get "$S" part:lead gain)" = 1.25 || fail "redo gain"
"$WB" history "$S" | grep -q '5 edits, 5 applied' || { "$WB" history "$S"; fail "history"; }

# The fixture session round-trips through fmt.
cp "$FIXTURE" "$ROOT/f.mws"
"$WB" fmt "$ROOT/f.mws" | cmp -s - "$FIXTURE" || fail "fmt is not canonical"
"$WB" check "$ROOT/f.mws" --no-files >/dev/null || fail "fixture check"
"$WB" get "$ROOT/f.mws" rule:#10 | grep -qx 'velocity_scale=11/20' || fail "get rule block"

# Checkpoint: journal cleared, small text committed, journals ignored.
"$WB" checkpoint "$S" v1 >/dev/null
test ! -e "$S.journal" || fail "checkpoint must clear the journal"
test "$(git -C "$WB_HOME" rev-list --count HEAD)" = 2 || fail "checkpoint commit"
git -C "$WB_HOME" log -1 --format=%B | grep -qx 'demo: checkpoint v1' || fail "commit message"
git -C "$WB_HOME" log -1 --format=%B | grep -qi 'co-authored' && fail "no trailers"
git -C "$WB_HOME" ls-files | grep -qx 'demo/sources/in.mid' || fail "sources are committed"
"$WB" checkpoint "$S" v1-again | grep -q 'nothing to commit' || fail "empty checkpoint"

# A hand edit behind the journal's back sets the journal aside.
"$WB" set "$S" part:lead gain 1.5 >/dev/null 2>&1
sed -i 's/^gain=1.5$/gain=1.75/' "$S"
"$WB" set "$S" part:lead gain 2 2>"$ROOT/err" >/dev/null
grep -q 'moved to' "$ROOT/err" || fail "stale journal not reported"
test -f "$S.journal.stale" || fail "stale journal kept"

echo "wb_cli: ok"
