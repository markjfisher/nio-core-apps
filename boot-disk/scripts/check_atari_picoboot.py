#!/usr/bin/env python3
"""Check the PicoBoot binary whose resident entry points the selector uses."""
import hashlib
import sys
from pathlib import Path

EXPECTED = "4d2307076b6a31939b16ff129c8429af2df6e3f34cd0b1fe10283da6e0eb034c"
path = Path(sys.argv[1])
if hashlib.sha256(path.read_bytes()).hexdigest() != EXPECTED:
    raise SystemExit(f"{path}: incompatible PicoBoot DD binary; update and test the Atari selector")
