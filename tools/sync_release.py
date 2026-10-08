#!/usr/bin/env python3
"""I validate the SDK-built FAP; synchronize local distribution copies and hashes.

No network, commits, pushes or release uploads. I run --sync after ufbt;
I run --check before packaging. Python standard library only."""
import argparse
import hashlib
import json
import os
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "source/fobworks_flipper/dist"
# I preserve the original 84,921-byte combined ceiling. I move 512 bytes from
# readonly-data headroom to code, and I reserve another 512 bytes for FOBscan's
# owned locking display model (payload + mutex/wrapper/allocator overhead).
# I transfer the budget without increasing the allowed RAM footprint.
LIMITS = {".text": 61864, ".rodata": 16621, ".bss": 5924}
TOTAL_HOST_RAM_LIMIT = 84921
FOBSCAN_MODEL_RESERVE = 512
FILES = ("fobworks_flipper.fap", "fw_catalog.fal", "fw_force.fal")


def sections(path):
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError(f"{path}: expected little-endian ELF32")
    offset = struct.unpack_from("<I", data, 32)[0]
    size, count, names_index = struct.unpack_from("<HHH", data, 46)
    entries = [struct.unpack_from("<IIIIIIIIII", data, offset + i * size)
               for i in range(count)]
    names = entries[names_index]
    table = data[names[4]:names[4] + names[5]]
    result = {}
    for entry in entries:
        name = table[entry[0]:].split(b"\0", 1)[0].decode()
        result[name] = (entry[5], data[entry[4]:entry[4] + entry[5]])
    meta = result[".fapmeta"][1]
    if struct.unpack_from("<IIHHH", meta) != (0x52474448, 1, 1, 87, 7):
        raise ValueError(f"{path}: expected Target 7 / API 87.1 manifest")
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sync", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.sync == args.check:
        parser.error("choose exactly one of --sync or --check")
    all_sections = {name: sections(DIST / name) for name in FILES}
    host = all_sections[FILES[0]]
    for section, limit in LIMITS.items():
        if host[section][0] > limit:
            raise ValueError(f"{section} exceeds recorded host budget: {host[section][0]} > {limit}")
    if sum(host[section][0] for section in LIMITS) + FOBSCAN_MODEL_RESERVE > TOTAL_HOST_RAM_LIMIT:
        raise ValueError("host sections plus display-model reserve exceed the original RAM ceiling")
    binary = (DIST / FILES[0]).read_bytes()
    for label in (b"FOBfreq timing", b"FOBtrack RX", b"FOBroll RX", b"Observation %s"):
        if label not in binary:
            raise ValueError(f"missing current scene label: {label!r}")
    digest = hashlib.sha256(binary).hexdigest()
    sums = "".join(f"{hashlib.sha256((DIST / name).read_bytes()).hexdigest()}  "
                   f"source/fobworks_flipper/dist/{name}\n" for name in FILES)
    sums += f"{digest}  FAP/fobworks_flipper.fap\n"
    baseline = (
        "# FAP size baseline — official SDK 1.4.3, Target 7, API 87.1\n\n"
        "I generated these figures from the built images with tools/sync_release.py.\n"
        "These sizes do not tell me whether heap and stack usage are safe on hardware.\n\n"
        "| Host section | Bytes | Recorded budget | Headroom |\n"
        "|---|---:|---:|---:|\n"
    )
    for name, limit in LIMITS.items():
        actual = host[name][0]
        baseline += f"| `{name}` | {actual} | {limit} | {limit - actual} |\n"
    baseline += "\n| Embedded plugin | .text | .rodata | .data | .bss | Resident sections |\n|---|---:|---:|---:|---:|---:|\n"
    for name in FILES[1:]:
        sec = all_sections[name]
        sizes = [sec.get(section, (0, b""))[0]
                 for section in (".text", ".rodata", ".data", ".bss")]
        baseline += f"| `{name}` | " + " | ".join(map(str, sizes)) + f" | {sum(sizes)} |\n"
    baseline += f"\nEmbedded assets: {host['.fapassets'][0]} bytes.\n\nHost SHA-256: `{digest}`.\n"
    readme_path = ROOT / "README.txt"
    readme = readme_path.read_text()
    readme = re.sub(
        r"(?m)^Loader sizes for this host image \(limits \d+ / \d+ / \d+\):$",
        "Loader sizes for this host image (limits "
        f"{LIMITS['.text']} / {LIMITS['.rodata']} / {LIMITS['.bss']}):",
        readme)
    readme = re.sub(r"(?m)^  \.text \d+, \.rodata \d+, \.bss \d+\.$",
                    f"  .text {host['.text'][0]}, .rodata {host['.rodata'][0]}, "
                    f".bss {host['.bss'][0]}.", readme)
    readme = re.sub(r"(SHA-256 of the copy in this repository:\n)[0-9a-f]{64}",
                    lambda match: match[1] + digest, readme)
    manifest = json.dumps({
        "source_revision": os.environ.get("GITHUB_SHA", "uncommitted local source snapshot"),
        "target": 7, "api": "87.1", "sdk": "official 1.4.3",
        "sdk_sha256": "2e89e70c6b5770440cbf02f2ca01a2f8804e05ddb77f66afdd23ed2584740c7f",
        "sha256": {name: hashlib.sha256((DIST / name).read_bytes()).hexdigest()
                   for name in FILES},
        "host_sections": {name: host[name][0] for name in LIMITS},
        "qualification": "I checked the SDK build and host regressions; full hardware testing is still pending",
    }, indent=2) + "\n"
    outputs = {
        ROOT / "FAP/fobworks_flipper.fap": binary,
        ROOT / "SHA256SUMS": sums.encode(),
        ROOT / "SIZE_BASELINE.md": baseline.encode(),
        ROOT / "BUILD_MANIFEST.json": manifest.encode(),
        readme_path: readme.encode(),
    }
    for path, content in outputs.items():
        if args.sync:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        elif not path.exists() or path.read_bytes() != content:
            raise ValueError(f"stale/missing distribution file: {path.relative_to(ROOT)}; run --sync")
    print(f"Distribution verified: Target 7 / API 87.1, SHA-256 {digest}")


if __name__ == "__main__":
    main()
