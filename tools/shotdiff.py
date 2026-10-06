#!/usr/bin/env python3
"""Compare two folders of screenshots from the autotest's shots mode.

    tools/shotdiff.py <baseline dir> <candidate dir> [--diff-dir DIR] [--tolerance N] [--max-differing P]

For every PNG in the baseline folder, reports how many pixels differ in the
candidate (a channel differing by more than --tolerance, default 0) and the
largest channel difference. A pair passes if at most --max-differing percent
of its pixels differ (default 0): animations run on real time, so two runs of
the same build are not pixel-identical (docs/renderer.md, "Screenshot
harness"). With --diff-dir, writes a PNG per differing pair that shows the
baseline dimmed with the differing pixels in magenta.
Exit status 1 if any pair fails or is missing.

Standard library only: reads the 8-bit RGB/RGBA non-interlaced PNGs the game
writes (cISC43DRender::TakeSnapshot), nothing more.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path


def read_png(path):
    """Returns (width, height, channels, bytearray of pixels, rows top first)."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, header = 8, [], None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    width, height, depth, color, _, _, interlace = header
    channels = {2: 3, 6: 4}.get(color)
    if depth != 8 or channels is None or interlace:
        raise ValueError(f"{path}: unsupported PNG (depth {depth}, color {color}, interlace {interlace})")
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)
    for y in range(height):
        filt = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if filt == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif filt != 0:
            raise ValueError(f"{path}: bad filter {filt} on row {y}")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, out


def write_png(path, width, height, rgb):
    rows = b"".join(b"\x00" + bytes(rgb[y * width * 3:(y + 1) * width * 3]) for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def compare(base_path, cand_path, tolerance, diff_path):
    """Returns (differing pixels, total pixels, max channel delta)."""
    if base_path.read_bytes() == cand_path.read_bytes():
        return 0, None, 0
    w1, h1, c1, a = read_png(base_path)
    w2, h2, c2, b = read_png(cand_path)
    if (w1, h1) != (w2, h2):
        raise ValueError(f"size {w1}x{h1} vs {w2}x{h2}")
    # Compare colour only: the game's alpha channel carries no picture.
    channels = 3
    differing, worst = 0, 0
    mask = bytearray(w1 * h1)
    for p in range(w1 * h1):
        i, j = p * c1, p * c2
        delta = max(abs(a[i + k] - b[j + k]) for k in range(channels))
        if delta > tolerance:
            differing += 1
            mask[p] = 1
        worst = max(worst, delta)
    if diff_path and differing:
        rgb = bytearray(w1 * h1 * 3)
        for p in range(w1 * h1):
            if mask[p]:
                rgb[p * 3:p * 3 + 3] = b"\xff\x00\xff"
            else:
                i = p * c1
                rgb[p * 3:p * 3 + 3] = bytes(v // 3 for v in a[i:i + 3])
        write_png(diff_path, w1, h1, rgb)
    return differing, w1 * h1, worst


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--diff-dir", type=Path)
    parser.add_argument("--tolerance", type=int, default=0)
    parser.add_argument("--max-differing", type=float, default=0.0)
    args = parser.parse_args()
    if args.diff_dir:
        args.diff_dir.mkdir(parents=True, exist_ok=True)

    failed = 0
    names = sorted(p.name for p in args.baseline.glob("*.png"))
    if not names:
        print(f"no PNGs in {args.baseline}", file=sys.stderr)
        return 1
    for name in names:
        cand = args.candidate / name
        if not cand.exists():
            print(f"{name}: MISSING")
            failed += 1
            continue
        diff = args.diff_dir / name if args.diff_dir else None
        try:
            differing, total, worst = compare(args.baseline / name, cand, args.tolerance, diff)
        except ValueError as error:
            print(f"{name}: ERROR {error}")
            failed += 1
            continue
        if differing == 0:
            print(f"{name}: same" + (" (identical files)" if total is None else f" (max delta {worst})"))
            continue
        percent = 100.0 * differing / total
        verdict = "within limit" if percent <= args.max_differing else "FAIL"
        if verdict == "FAIL":
            failed += 1
        print(f"{name}: {differing} of {total} pixels differ ({percent:.3f}%), max delta {worst}: {verdict}")
    print(f"{len(names) - failed} of {len(names)} match")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
