#!/bin/sh
# Every WAV that make_sfx.py writes, as Ogg Vorbis (quality 4), and the WAV removed: the
# page carries a third of the bytes. Run in sfx/ after make_sfx.py.
set -e
for f in *.wav; do
    ffmpeg -hide_banner -loglevel error -y -i "$f" -c:a libvorbis -q:a 4 "${f%.wav}.ogg"
    rm "$f"
done
