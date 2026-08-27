#!/usr/bin/env python3
"""
Encrypt DTS shapes into LFXE containers for the LiFx client modpack.

Walks a directory for *.dts (and *.cached.dts), wraps each in an LFXE
container (ChaCha20 under the project key), and writes it back. Only the
LiFx client -- which carries the matching baked key -- can load the result;
a stock DTS viewer or an unmodified client sees garbage and fails the
version gate. Vanilla and encrypted shapes coexist, so partial encryption
is fine.

Idempotent: files that already begin with the LFXE magic are skipped, so
re-running over a staging tree (or running twice in a pipeline) is safe.

The key comes from config/dts_key.bin -- the SAME file the client build
(scripts/gen_baked_key.py -> lfxe_key_data.h) bakes in. Keep them in sync;
re-baking the key requires rebuilding the client DLL.

Usage:
  python3 scripts/dts_encrypt.py <dir> [--key config/dts_key.bin]
                                       [--key-id N] [--dry-run] [--quiet]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dts_lib as L

DEFAULT_KEY = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "config", "dts_key.bin"
)
DEFAULT_EXTS = (".dts",)  # ".dts" also matches ".cached.dts"; ".dso" matches ".cs.dso"


def iter_assets(root, suffixes):
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if name.lower().endswith(suffixes):
                yield os.path.join(dirpath, name)


def main():
    ap = argparse.ArgumentParser(description="Encrypt DTS shapes into LFXE containers.")
    ap.add_argument("dir", help="directory to walk (encrypted in place)")
    ap.add_argument("--key", default=DEFAULT_KEY, help="32-byte key file (default config/dts_key.bin)")
    ap.add_argument("--key-id", type=int, default=0, help="LFXE keyId (0 = baked key)")
    ap.add_argument("--ext", default="dts",
                    help="comma-separated extensions to encrypt (default 'dts'; "
                         "use 'dso' for *.cs.dso, or 'dts,dso' for both)")
    ap.add_argument("--dry-run", action="store_true", help="report only, write nothing")
    ap.add_argument("--quiet", action="store_true", help="summary line only")
    args = ap.parse_args()

    suffixes = tuple("." + e.strip().lstrip(".").lower() for e in args.ext.split(",") if e.strip())

    if not os.path.isdir(args.dir):
        print(f"error: not a directory: {args.dir}", file=sys.stderr)
        return 2
    try:
        key = open(args.key, "rb").read()
    except OSError as e:
        print(f"error: cannot read key {args.key}: {e}", file=sys.stderr)
        return 2
    if len(key) != L.KEY_SIZE:
        print(f"error: key must be {L.KEY_SIZE} bytes, got {len(key)}", file=sys.stderr)
        return 2

    enc = skipped = 0
    for path in iter_assets(args.dir, suffixes):
        with open(path, "rb") as f:
            data = f.read()
        if L.has_magic(data):
            skipped += 1
            continue
        if not args.quiet:
            rel = os.path.relpath(path, args.dir)
            print(f"  encrypt {rel} ({len(data)} -> {len(data) + L.HEADER_SIZE} bytes)"
                  + (" [dry-run]" if args.dry_run else ""))
        if not args.dry_run:
            container = L.pack(data, key, key_id=args.key_id)
            tmp = path + ".lfxe.tmp"
            with open(tmp, "wb") as f:
                f.write(container)
            os.replace(tmp, path)  # atomic; never leave a half-written shape
        enc += 1

    verb = "would encrypt" if args.dry_run else "encrypted"
    print(f"LFXE pack [{','.join(suffixes)}]: {verb} {enc}, skipped {skipped} already-LFXE, "
          f"key={os.path.basename(args.key)} id={args.key_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
