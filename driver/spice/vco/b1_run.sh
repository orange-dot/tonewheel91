#!/bin/bash
# Runs every <out>/spice/<case>/bench.cir that has no trace yet, two at a time.
# Usage: b1_run.sh build/vco/b1     (wrap long runs in systemd-inhibit)
cd "${1:?output folder}/spice" || exit 1
ls -d */ | tr -d / | while read c; do [ -e $c/trace.tsv ] || [ -e $c/trace.raw ] || echo $c; done |
  xargs -P 2 -I{} sh -c 'cd {} && ngspice -b bench.cir > ngspice.log 2>&1; echo "{} done $(grep -o "Total analysis time.*" ngspice.log)"'
