#!/usr/bin/env bash
# Headless screenshots of every rite: tools/shots.sh [outdir]
set -e
OUT=${1:-shots}; mkdir -p "$OUT"
BIN=.pio/build/native/program
run() { SDL_VIDEODRIVER=offscreen GRIM_WAV="$OUT/$1.wav" GRIM_SCRIPT="$2" timeout 30 $BIN || true; }
run menu   "2800:enter;3000:esc;3300:right;3500:left;4200:shot:$OUT/menu.ppm;4300:quit"
run splash "1500:shot:$OUT/splash.ppm;1600:quit"
run drone  "2800:key:1;3200:enter;9000:shot:$OUT/drone.ppm;12000:quit"
run seq    "2800:key:2;5000:right;5100:right;5200:down;5300:down;5400:down;6000:shot:$OUT/seq.ppm;10000:quit"
run beat   "2800:key:3;4000:tab;4200:tab;6500:shot:$OUT/beat.ppm;7000:key:t*(t>>5|t>>8)&+;7300:enter;7600:shot:$OUT/beat-err.ppm;10000:quit"
run wired  "2800:key:4;5000:shot:$OUT/wired-torus.ppm;5200:key:2;6500:shot:$OUT/wired-navi.ppm;6600:key:3;8000:shot:$OUT/wired-solids.ppm;8100:key:4;9000:shot:$OUT/wired-tunnel.ppm;9200:quit"
run visions "2800:key:5;4500:shot:$OUT/vis-plasma.ppm;4600:key:2;5500:shot:$OUT/vis-gate.ppm;5600:key:3;7500:shot:$OUT/vis-fire.ppm;7600:key:4;9500:shot:$OUT/vis-rain.ppm;9600:key:5;10500:shot:$OUT/vis-kaleido.ppm;10600:quit"
run oracle "2800:key:6;3500:enter;6000:shot:$OUT/oracle.ppm;8000:quit"
python3 -I - "$OUT" <<'PY'
import sys, glob
from PIL import Image
for p in glob.glob(sys.argv[1] + "/*.ppm"):
    Image.open(p).resize((720, 405), Image.NEAREST).save(p[:-4] + ".png")
PY
rm -f "$OUT"/*.ppm
