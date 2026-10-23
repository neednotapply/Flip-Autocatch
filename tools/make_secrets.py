#!/usr/bin/env python3
"""Create pgp_device.txt for Flip Autocatch from your Pokemon GO Plus dump.

Reads the MAC, device key and blob from either
  * a C header/source with byte arrays (the old pgp_device.h, pgpemu's secrets.c), or
  * pgpemu's secrets.yaml (inline [0x.., ..] lists, first device), or
  * --mac/--key/--blob hex strings,
and writes the FlipperFormat file the app loads from
/ext/apps_data/autocatch/pgp_device.txt.

Examples:
  python3 tools/make_secrets.py pgp_device.h -o pgp_device.txt
  python3 tools/make_secrets.py --mac 98B6E9123456 --key 00112233... --blob ... -o pgp_device.txt
"""

import argparse
import re
import sys

LENGTHS = {"mac": 6, "key": 16, "blob": 256}

C_ARRAY = re.compile(r"(\w+)\s*\[\s*(\d*)\s*\]\s*=\s*\{([^}]*)\}", re.S)
YAML_LIST = re.compile(r"^\s*-?\s*(name|mac|key|blob)\s*:\s*(\[[^\]]*\]|.+)$", re.M)


def parse_bytes(text):
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
    return [int(token, 0) for token in re.findall(r"0[xX][0-9a-fA-F]+|\d+", text)]


def classify(name):
    upper = name.upper()
    if "BLOB" in upper:
        return "blob"
    if "KEY" in upper:
        return "key"
    if "MAC" in upper:
        return "mac"
    return None


def parse_file(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    secrets = {}

    if path.endswith((".yaml", ".yml")):
        for key, value in YAML_LIST.findall(text):
            if key in secrets:
                continue  # only the first device
            if key == "name":
                secrets[key] = value.strip().strip("'\"")
            else:
                secrets[key] = parse_bytes(value)
        return secrets

    for name, size, body in C_ARRAY.findall(text):
        kind = classify(name)
        if kind and kind not in secrets:
            values = parse_bytes(body)
            # C zero-fills missing initializers
            if size and int(size) > len(values):
                values += [0] * (int(size) - len(values))
            secrets[kind] = values
    return secrets


def hex_arg(value):
    cleaned = re.sub(r"[^0-9a-fA-F]", "", value)
    if len(cleaned) % 2:
        raise argparse.ArgumentTypeError("odd number of hex digits")
    return list(bytes.fromhex(cleaned))


def format_hex(values):
    return " ".join(f"{b:02X}" for b in values)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", nargs="?", help="C header/source or pgpemu secrets.yaml")
    parser.add_argument("--name", help="device name shown in the app")
    parser.add_argument("--mac", type=hex_arg, help="6 byte MAC, display order (98:B6:E9:...)")
    parser.add_argument("--key", type=hex_arg, help="16 byte device key")
    parser.add_argument("--blob", type=hex_arg, help="256 byte blob")
    parser.add_argument("-o", "--output", default="-", help="output file (default: stdout)")
    args = parser.parse_args()

    secrets = parse_file(args.source) if args.source else {}
    for kind in ("name", "mac", "key", "blob"):
        if getattr(args, kind) is not None:
            secrets[kind] = getattr(args, kind)

    for kind, length in LENGTHS.items():
        if kind not in secrets:
            sys.exit(f"error: no {kind} found")
        if len(secrets[kind]) != length or any(b > 0xFF for b in secrets[kind]):
            sys.exit(f"error: {kind} must be {length} bytes, got {len(secrets[kind])}")

    lines = [
        "Filetype: Flip Autocatch PGP",
        "Version: 1",
        "# Secrets dumped from your own Pokemon GO Plus. Keep this file private.",
        f"Name: {secrets.get('name') or 'Pokemon GO Plus'}",
        f"MAC: {format_hex(secrets['mac'])}",
        f"Device Key: {format_hex(secrets['key'])}",
        f"Blob: {format_hex(secrets['blob'])}",
    ]
    output = "\n".join(lines) + "\n"

    if args.output == "-":
        sys.stdout.write(output)
    else:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(output)
        print(f"Wrote {args.output}; copy it to /ext/apps_data/autocatch/pgp_device.txt", file=sys.stderr)


if __name__ == "__main__":
    main()
