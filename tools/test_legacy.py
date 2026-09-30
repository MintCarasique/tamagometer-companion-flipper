"""Compile and exercise the production C decoder using captured V4/V3 signals."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    compiler = shutil.which(args.cc)
    if not compiler:
        parser.error("Host C compiler not found; pass --cc gcc, clang, or a TinyCC executable")
    root = Path(__file__).resolve().parents[1]
    fixtures = json.loads((root / "tests/fixtures/v4_captures.json").read_text(encoding="utf-8"))
    rows = []
    for frame in fixtures:
        expected = bytes.fromhex(frame["data"]) if frame["data"] else b""
        rows.append(" ".join(map(str, (len(expected), len(frame["raw"]), *expected, *frame["raw"]))))
    with tempfile.TemporaryDirectory(prefix="tama-legacy-") as temporary:
        executable = Path(temporary) / ("legacy_test.exe" if os.name == "nt" else "legacy_test")
        subprocess.run([
            compiler, "-Wall", "-Wextra", "-Werror", "-I", str(root),
            str(root / "tests/legacy_test.c"), str(root / "tamagometer_legacy.c"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], input="\n".join(rows), text=True, check=True)


if __name__ == "__main__":
    main()
