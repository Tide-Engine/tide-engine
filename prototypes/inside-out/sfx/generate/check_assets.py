"""Verify reviewed asset hashes and PCM contracts without an audio device.

With an optional directory argument, check regenerated assets instead of the
shipping set. Only the standard library is needed.
"""
from array import array
import hashlib
from pathlib import Path
import sys
import wave


def main():
    shipping = Path(__file__).resolve().parents[1]
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else shipping
    total = 0
    entries = (shipping / "SHA256SUMS").read_text().splitlines()
    for entry in entries:
        expected, name = entry.split()
        path = root / name
        data = path.read_bytes()
        assert hashlib.sha256(data).hexdigest() == expected, f"changed asset: {path}"
        total += len(data)
        if path.suffix == ".wav":
            with wave.open(str(path)) as wav:
                assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 48000), name
                pcm = array("h", wav.readframes(wav.getnframes()))
            if sys.byteorder != "little":
                pcm.byteswap()
            assert pcm[0] == pcm[-1] == 0, f"nonzero endpoints: {name}"
            assert max(abs(sample) for sample in pcm) <= 16423, f"peak over -6 dBFS: {name}"
        else:
            assert data.startswith(b"OggS"), f"invalid Ogg header: {name}"
    print(f"{len(entries)} reviewed assets verified; {total:,} bytes; PCM formats/endpoints/peaks pass")


if __name__ == "__main__":
    main()
