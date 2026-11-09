#!/bin/sh
# Install everything a full tonewheel91 build needs on Fedora:
# `make test`, `make test-clang`, `make sanitize`, `make fuzz-smf`,
# `make fuzz-mws`, the live drivers (ALSA), and the Workbench UI (SDL3).
#
#   ./install-deps.sh           install what is missing (asks for sudo)
#   ./install-deps.sh --check   only report; exit 1 when something is missing
#
# Other distributions: the package list below names what each item is for;
# install the equivalents by hand.
set -eu

# package                 needed by
PACKAGES="
make                      all builds
gcc                       default compiler (C23)
clang                     test-clang, sanitize, FUZZ_CC
compiler-rt               clang ASan/UBSan runtimes and libFuzzer
alsa-lib-devel            live drivers and test_hosted (-lasound)
SDL3-devel                Workbench UI (make workbench)
pkgconf-pkg-config        finds SDL3 for the Workbench build
git                       Workbench session root history
python3                   CLI checks in make test
adwaita-mono-fonts        Workbench UI font (any listed monospace font works)
"

check_only=false
case "${1:-}" in
    --check) check_only=true ;;
    "") ;;
    *) echo "usage: $0 [--check]" >&2; exit 2 ;;
esac

if ! command -v rpm >/dev/null 2>&1 || ! command -v dnf >/dev/null 2>&1; then
    echo "install-deps: this script uses dnf/rpm (Fedora). Needed packages:" >&2
    echo "$PACKAGES" | sed '/^$/d; s/^/  /' >&2
    exit 1
fi

missing=""
for name in $(echo "$PACKAGES" | awk 'NF { print $1 }'); do
    if rpm -q "$name" >/dev/null 2>&1; then
        printf '  ok       %s\n' "$name"
    else
        printf '  missing  %s\n' "$name"
        missing="$missing $name"
    fi
done

if [ -z "$missing" ]; then
    echo "install-deps: everything is installed"
elif $check_only; then
    echo "install-deps: missing:$missing"
    exit 1
else
    echo "install-deps: installing:$missing"
    if [ "$(id -u)" -eq 0 ]; then
        dnf install -y $missing
    else
        sudo dnf install -y $missing
    fi
fi

# What the Makefile will actually see.
if pkg-config --exists sdl3 2>/dev/null; then
    echo "install-deps: pkg-config sdl3 $(pkg-config --modversion sdl3)"
else
    echo "install-deps: pkg-config cannot find sdl3; make workbench will refuse" >&2
    exit 1
fi
if ! pkg-config --exists alsa 2>/dev/null; then
    echo "install-deps: pkg-config cannot find alsa" >&2
    exit 1
fi
echo "install-deps: next: make test && make test-workbench && make workbench"
