#!/usr/bin/env python3
"""Builds the LittleFS content of the ESP32-S3 firmware from the web UI in server/.

Text files (HTML, JS, CSS, JSON, ...) are stored gzip compressed as <name>.gz,
ESPAsyncWebServer serves them with "Content-Encoding: gzip" when <name> is
requested. Other files (images, ...) are copied unchanged. json/toytags.json is
skipped, the firmware generates it from the saved tokens.

Usage: python3 esp32/tools/build_data.py [output directory]
The default output directory is esp32/LDToyPadEmulator/data (the folder the
Arduino IDE LittleFS upload plugin uses).
"""

import gzip
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "server"
DEFAULT_OUTPUT = ROOT / "esp32" / "LDToyPadEmulator" / "data"

COMPRESS = {".html", ".htm", ".js", ".css", ".json", ".svg", ".ico", ".txt", ".map"}
SKIP = {Path("json") / "toytags.json"}


def main() -> int:
    output = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_OUTPUT
    if output == SOURCE or SOURCE in output.parents:
        print("The output directory must not be inside server/", file=sys.stderr)
        return 1
    if output.exists():
        shutil.rmtree(output)

    total_in = total_out = 0
    for src in sorted(p for p in SOURCE.rglob("*") if p.is_file()):
        rel = src.relative_to(SOURCE)
        if rel in SKIP:
            continue
        data = src.read_bytes()
        dest = output / rel
        if src.suffix.lower() in COMPRESS:
            # mtime=0 keeps the output reproducible
            data = gzip.compress(data, compresslevel=9, mtime=0)
            dest = dest.with_name(dest.name + ".gz")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        total_in += src.stat().st_size
        total_out += len(data)

    print(f"Wrote {output} ({total_in} -> {total_out} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
