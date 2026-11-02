from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[2]
names = subprocess.check_output(['git', 'diff', '--name-only'], cwd=root, text=True).splitlines()
for name in names:
    path = root / name
    raw = path.read_bytes()
    try:
        raw.decode('utf-8')
    except UnicodeDecodeError:
        text = raw.decode('utf-8', errors='surrogateescape')
        replaced = []
        for char in text:
            code = ord(char)
            if 0xDC80 <= code <= 0xDCFF:
                replaced.append(bytes([code - 0xDC00]).decode('cp1252'))
            else:
                replaced.append(char)
        path.write_text(''.join(replaced), encoding='utf-8', newline='')
        print('Repaired non-UTF8 bytes:', name)
