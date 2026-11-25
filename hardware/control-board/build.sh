#!/bin/sh
# Regenerate everything from design.py + layout.py: symbols, schematic, board (placement, hand-defined routes,
# power fanout, planes, stitching, silkscreen with its checks), ERC, DRC with schematic parity, fab outputs.
# Needs KiCad 10. Nothing is autorouted.
set -e
cd "$(dirname "$0")"
KP=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
KC=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
quiet() { grep -v "wxApp\|memory leak" || true; }
mkdir -p out
python3 mk_sym.py
python3 gen_sch.py
if ! $KP gen_pcb.py > out/gen_pcb.log 2>&1; then quiet < out/gen_pcb.log; echo "gen_pcb failed"; exit 1; fi
quiet < out/gen_pcb.log
$KC sch erc --severity-error -o out/erc.rpt control.kicad_sch 2>&1 | grep -v Fontconfig | tail -1
$KC pcb drc --severity-error --schematic-parity -o out/drc.rpt control.kicad_pcb 2>&1 | grep -v Fontconfig | tail -3
python3 fab.py
