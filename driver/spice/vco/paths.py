"""Where the oscillator study keeps things: sources beside this file, every
generated file under build/vco/<study>/ (or $VCO_OUT_B1 / $VCO_OUT_VCO2)."""
import os, pathlib

SRC = pathlib.Path(__file__).resolve().parent
ROOT = SRC.parents[2]
MA_REF = ROOT / 'build' / 'vco_ma_ref'      # make build/vco_ma_ref

def out(study):
    d = pathlib.Path(os.environ.get(f'VCO_OUT_{study.upper()}', ROOT / 'build' / 'vco' / study)).resolve()
    d.mkdir(parents=True, exist_ok=True)
    return d
