#!/usr/bin/env python3
"""Mask or unmask the FLIPPER_MFR_KEYS table used by the Flipper app.

The decoder needs these keys to derive and decrypt KeeLoq frames, so they must
remain in the app. This transform makes the values less readable in source and
in the built FAP; the app reverses it before use.

    mask(k)   = ROTL(k ^ A, N) ^ B
    unmask(v) = ROTR(v ^ B, N) ^ A

This is obfuscation, not a security boundary. The transform and its constants
are visible in the source and can be reversed. Use --unmask to restore the
plaintext table.

Usage:
    python3 tools/mask_mfrkeys.py            # plaintext -> masked
    python3 tools/mask_mfrkeys.py --unmask   # masked -> plaintext
    python3 tools/mask_mfrkeys.py --check    # report state, change nothing
"""

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
TABLE = HERE / "source/fobworks_flipper/protocol/flipper_keeloq.c"

# Keep these values in sync with the MFR_KEY_* definitions in C.
A = 0x5A5A5A5A5A5A5A5A
B = 0x3C3C3C3C3C3C3C3C
N = 13

M64 = (1 << 64) - 1


def rotl(x, n):
    n &= 63
    return ((x << n) | (x >> (64 - n))) & M64 if n else x


def rotr(x, n):
    n &= 63
    return ((x >> n) | (x << (64 - n))) & M64 if n else x


def encode(k):
    return rotl((k ^ A) & M64, N) ^ B


def decode(v):
    return rotr((v ^ B) & M64, N) ^ A


# Matches a manufacturer-key row: { "Name", 0xHEXULL, learn }.
ENTRY = re.compile(
    r'(?P<pre>\{\s*"(?P<name>[^"]+)"\s*,\s*0x)(?P<key>[0-9A-Fa-f]{16})(?P<post>ULL\s*,\s*\d+\s*\})'
)
BLOCK = re.compile(
    r"(?P<open>const MfrKey FLIPPER_MFR_KEYS\[N_MFR_KEYS\]\s*=\s*\{)(?P<body>.*?)(?P<close>\n\};)",
    re.S,
)


def entries(src):
    m = BLOCK.search(src)
    if not m:
        sys.exit(f"FLIPPER_MFR_KEYS block not found in {TABLE}")
    return m, list(ENTRY.finditer(m.group("body")))


def is_masked(keys):
    """Masked iff decoding turns the stored values into the structural defaults."""
    defaults = {0x0000000000000000, 0xFFFFFFFFFFFFFFFF}
    as_is = sum(1 for k in keys if k in defaults)
    flipped = sum(1 for k in keys if decode(k) in defaults)
    if flipped != as_is:
        return flipped > as_is
    return all(encode(k) == k for k in keys[:8])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--unmask", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    src = TABLE.read_text(encoding="utf-8")
    m, es = entries(src)
    if not es:
        sys.exit("no entries parsed -- refusing to touch the table")
    keys = [int(e.group("key"), 16) for e in es]
    masked = is_masked(keys)

    for k in keys:
        assert decode(encode(k)) == k, f"encode/decode not inverse on {k:#018x}"

    print(f"  file    : {TABLE.relative_to(HERE)}")
    print(f"  entries : {len(es)}")
    print(f"  state   : {'MASKED' if masked else 'PLAINTEXT'}")
    if args.check:
        return 0

    want_masked = not args.unmask
    if want_masked == masked:
        print(f"  no-op   : already {'masked' if masked else 'plaintext'}")
        return 0

    fn = encode if want_masked else decode
    body, last, out = m.group("body"), 0, []
    for e in es:
        k = int(e.group("key"), 16)
        nk = fn(k)
        assert nk != k, f"{e.group('name')} unchanged by the transform"
        out.append(body[last:e.start()])
        out.append(f"{e.group('pre')}{nk:016X}{e.group('post')}")
        last = e.end()
    out.append(body[last:])

    new_src = src[:m.start("body")] + "".join(out) + src[m.end("body"):]
    TABLE.write_text(new_src, encoding="utf-8")

    _, es2 = entries(TABLE.read_text(encoding="utf-8"))
    keys2 = [int(e.group("key"), 16) for e in es2]
    assert is_masked(keys2) == want_masked, "written file is not in the intended state"
    inv = decode if want_masked else encode
    assert [inv(k) for k in keys2] == keys, "round-trip through the file does not restore the original keys"

    print(f"  wrote   : {'MASKED' if want_masked else 'PLAINTEXT'}")
    print(f"  verified: {len(keys2)} values round-trip to the originals")
    return 0


if __name__ == "__main__":
    sys.exit(main())
