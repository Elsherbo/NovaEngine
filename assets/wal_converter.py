"""
RedEye Engine -- WAL Texture Converter
========================================
Converts PNG/JPG/BMP/TGA/WAL files into palettized .wal files
using your colormap.pcx (raw 768-byte palette).

Usage:
    python wal_converter.py --palette colormap.pcx --input textures/ --output wal_out/

For existing .wal files from Quake, supply the original Quake palette as src-palette:
    python wal_converter.py --palette colormap.pcx --src-palette quake_palette.lmp --input textures/ --output wal_out/

Optional flags:
    --src-palette PATH   Palette used to DECODE existing .wal inputs (default: same as --palette)
    --preview            Save a side-by-side preview PNG for each texture
    --dither             Use Floyd-Steinberg dithering (better gradients, slower)
    --no-mips            Skip mip level generation (mip 0 only)
    --flags 0            WAL surface flags (default 0)
    --contents 0         WAL contents flags (default 0)

Requirements:
    python -m pip install Pillow numpy
"""

import argparse
import os
import struct
import sys
import time
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
except ImportError as _e:
    print(f"[ERROR] Missing dependencies: {_e}")
    print(f"  Python: {sys.executable}")
    print(f"  Fix:    {sys.executable} -m pip install Pillow numpy")
    sys.exit(1)


WAL_HEADER_SIZE = 100
WAL_MIP_LEVELS  = 4
WAL_NAME_LEN    = 32

SUPPORTED = {'.png', '.jpg', '.jpeg', '.bmp', '.tga', '.tiff', '.webp', '.wal'}

_W = np.array([0.299, 0.587, 0.114], dtype=np.float32)


# ---------------------------------------------------------------------------
# Palette
# ---------------------------------------------------------------------------

def load_palette(path):
    data = Path(path).read_bytes()
    if len(data) != 768:
        raise ValueError(f"Expected 768-byte palette, got {len(data)} bytes: {path}")
    return np.frombuffer(data, dtype=np.uint8).reshape(256, 3).copy()


# ---------------------------------------------------------------------------
# Color LUT
# ---------------------------------------------------------------------------

def build_lookup_table(palette):
    print("  Building color lookup table...", end=" ", flush=True)
    t0 = time.time()

    rv = np.arange(32, dtype=np.float32) * 8 + 4
    gv = np.arange(32, dtype=np.float32) * 8 + 4
    bv = np.arange(32, dtype=np.float32) * 8 + 4
    R, G, B = np.meshgrid(rv, gv, bv, indexing='ij')
    queries = np.stack([R, G, B], axis=-1)   # (32,32,32,3)

    pal_f = palette.astype(np.float32)
    lut = np.zeros((32, 32, 32), dtype=np.uint8)

    for ri in range(32):
        for gi in range(32):
            row  = queries[ri, gi]                           # (32, 3)
            diff = row[:, None, :] - pal_f[None, :, :]      # (32, 256, 3)
            dist = (diff ** 2 * _W).sum(axis=-1)             # (32, 256)
            lut[ri, gi] = dist.argmin(axis=-1).astype(np.uint8)

    print(f"done ({time.time()-t0:.2f}s)")
    return lut


# ---------------------------------------------------------------------------
# Palettize
# ---------------------------------------------------------------------------

def palettize(img_rgb, lut, dither=False, palette=None):
    h, w = img_rgb.shape[:2]
    img  = img_rgb.astype(np.float32)

    if dither and palette is not None:
        result = np.zeros((h, w), dtype=np.uint8)
        pal_f  = palette.astype(np.float32)
        for y in range(h):
            for x in range(w):
                old = img[y, x].clip(0, 255)
                ri  = int(old[0]) >> 3
                gi  = int(old[1]) >> 3
                bi  = int(old[2]) >> 3
                idx = lut[ri, gi, bi]
                result[y, x] = idx
                err = old - pal_f[idx]
                if x + 1 < w:
                    img[y, x+1]   += err * (7/16)
                if y + 1 < h:
                    if x > 0:
                        img[y+1, x-1] += err * (3/16)
                    img[y+1, x]       += err * (5/16)
                    if x + 1 < w:
                        img[y+1, x+1] += err * (1/16)
        return result
    else:
        qi = (img[..., 0].astype(np.int32) >> 3).clip(0, 31)
        qg = (img[..., 1].astype(np.int32) >> 3).clip(0, 31)
        qb = (img[..., 2].astype(np.int32) >> 3).clip(0, 31)
        return lut[qi, qg, qb]


# ---------------------------------------------------------------------------
# Mips
# ---------------------------------------------------------------------------

def make_mips(indexed, palette, lut, n_mips=4):
    mips = [indexed]
    h, w = indexed.shape
    rgb  = palette[indexed]

    for _ in range(n_mips - 1):
        h, w = h // 2, w // 2
        if h < 1 or w < 1:
            break
        pil = Image.fromarray(rgb).resize((w, h), Image.LANCZOS)
        rgb  = np.array(pil)
        mips.append(palettize(rgb, lut))

    return mips


# ---------------------------------------------------------------------------
# WAL writer
# ---------------------------------------------------------------------------

def write_wal(output_path, name, mips, flags=0, contents=0, value=0):
    h0, w0  = mips[0].shape
    n_mips  = min(len(mips), WAL_MIP_LEVELS)

    offsets = []
    current = WAL_HEADER_SIZE
    for m in mips[:n_mips]:
        offsets.append(current)
        current += m.size
    while len(offsets) < WAL_MIP_LEVELS:
        offsets.append(current)

    name_b = name.encode('ascii', errors='replace')[:WAL_NAME_LEN - 1]
    name_b = name_b.ljust(WAL_NAME_LEN, b'\x00')

    header = struct.pack('<32sII4I32sIII',
        name_b, w0, h0,
        *offsets[:4],
        b'\x00' * WAL_NAME_LEN,
        flags, contents, value,
    )
    assert len(header) == WAL_HEADER_SIZE

    with open(output_path, 'wb') as f:
        f.write(header)
        for m in mips[:n_mips]:
            f.write(m.flatten().tobytes())


# ---------------------------------------------------------------------------
# WAL reader
# ---------------------------------------------------------------------------

def read_wal(path, src_palette):
    """Decode a .wal file back to an RGB PIL Image using src_palette."""
    with open(path, 'rb') as f:
        data = f.read()

    w, h     = struct.unpack_from('<II', data, 32)
    offsets  = struct.unpack_from('<4I', data, 40)
    off      = offsets[0]
    n_pixels = w * h

    if off + n_pixels > len(data):
        raise ValueError(
            f"WAL truncated: need {n_pixels} px at offset {off}, "
            f"file is {len(data)} bytes"
        )

    indices = np.frombuffer(data, dtype=np.uint8, count=n_pixels, offset=off)
    rgb     = src_palette[indices.reshape(h, w)]
    return Image.fromarray(rgb, 'RGB')


# ---------------------------------------------------------------------------
# Preview
# ---------------------------------------------------------------------------

def save_preview(src_rgb, indexed, palette, output_path):
    pal_rgb = palette[indexed]
    h, w    = src_rgb.shape[:2]
    mh, mw  = h // 2, w // 2

    images  = [src_rgb, pal_rgb]
    if mh > 0 and mw > 0:
        images.append(np.array(Image.fromarray(pal_rgb).resize((mw, mh), Image.NEAREST)))

    max_h = max(img.shape[0] for img in images)
    strips = []
    for img in images:
        pad = np.zeros((max_h, img.shape[1], 3), dtype=np.uint8)
        pad[:img.shape[0]] = img
        strips.append(pad)

    Image.fromarray(np.concatenate(strips, axis=1)).save(output_path)


# ---------------------------------------------------------------------------
# Dimension helpers
# ---------------------------------------------------------------------------

def next_power_of_two(n):
    p = 1
    while p < n:
        p <<= 1
    return p


def fit_to_wal(img):
    w, h = img.size
    nw   = max(8, next_power_of_two(w))
    nh   = max(8, next_power_of_two(h))
    if nw != w or nh != h:
        print(f"    [warn] Resized {w}x{h} -> {nw}x{nh} (WAL requires power-of-2)")
        img = img.resize((nw, nh), Image.LANCZOS)
    return img


# ---------------------------------------------------------------------------
# Per-texture pipeline
# ---------------------------------------------------------------------------

def convert_texture(src_path, out_dir, palette, lut, args, src_palette):
    try:
        if src_path.suffix.lower() == '.wal':
            img = read_wal(str(src_path), src_palette)
        else:
            img = Image.open(src_path).convert('RGB')

        img     = fit_to_wal(img)
        rgb     = np.array(img)
        indexed = palettize(rgb, lut,
                            dither=args.dither,
                            palette=palette if args.dither else None)
        mips    = make_mips(indexed, palette, lut) if not args.no_mips else [indexed]

        tex_name = src_path.stem
        wal_path = out_dir / (tex_name + '.wal')
        write_wal(str(wal_path), tex_name, mips,
                  flags=args.flags, contents=args.contents)

        if args.preview:
            save_preview(rgb, indexed, palette,
                         str(out_dir / (tex_name + '_preview.png')))

        w, h     = img.size
        mip_str  = f"({len(mips)} mips)" if not args.no_mips else "(no mips)"
        size_kb  = wal_path.stat().st_size / 1024
        print(f"    OK  {tex_name}.wal  {w}x{h}  {mip_str}  {size_kb:.1f} KB")
        return True

    except Exception as e:
        print(f"    FAIL  {src_path.name}: {e}")
        return False


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="RedEye Engine WAL Texture Converter",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument('--palette',     required=True,
                        help='Target colormap.pcx to palettize INTO (768 bytes)')
    parser.add_argument('--src-palette', default=None,
                        help='Source palette to DECODE existing .wal inputs '
                             '(defaults to --palette if omitted)')
    parser.add_argument('--input',       required=True,
                        help='Source image/WAL file or folder')
    parser.add_argument('--output',      default='wal_out',
                        help='Output folder (default: wal_out/)')
    parser.add_argument('--preview',     action='store_true',
                        help='Save before/after preview PNGs')
    parser.add_argument('--dither',      action='store_true',
                        help='Floyd-Steinberg dithering')
    parser.add_argument('--no-mips',     action='store_true',
                        help='Skip mip level generation')
    parser.add_argument('--flags',       type=int, default=0,
                        help='WAL surface flags')
    parser.add_argument('--contents',    type=int, default=0,
                        help='WAL contents flags')
    args = parser.parse_args()

    # Load target palette
    print(f"\n[1/4] Loading target palette: {args.palette}")
    try:
        palette = load_palette(args.palette)
        print(f"      256 colors loaded OK")
    except Exception as e:
        print(f"[ERROR] {e}"); sys.exit(1)

    # Load source palette for decoding input WALs
    src_pal_path = args.src_palette or args.palette
    if src_pal_path == args.palette:
        src_palette = palette
        print(f"      Source palette: same as target")
    else:
        print(f"      Loading source palette: {src_pal_path}")
        try:
            src_palette = load_palette(src_pal_path)
            print(f"      256 colors loaded OK")
        except Exception as e:
            print(f"[ERROR] {e}"); sys.exit(1)

    # Build LUT
    print(f"[2/4] Building color LUT...")
    lut = build_lookup_table(palette)

    # Collect files
    print(f"[3/4] Scanning input: {args.input}")
    src = Path(args.input)
    if src.is_file():
        files = [src] if src.suffix.lower() in SUPPORTED else []
    else:
        files = [f for f in src.rglob('*') if f.suffix.lower() in SUPPORTED]

    if not files:
        print(f"[ERROR] No supported files found in '{args.input}'")
        print(f"        Supported: {', '.join(sorted(SUPPORTED))}")
        sys.exit(1)

    wal_count = sum(1 for f in files if f.suffix.lower() == '.wal')
    img_count = len(files) - wal_count
    print(f"      Found {len(files)} file(s)  ({wal_count} .wal + {img_count} images)")

    # Convert
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"[4/4] Converting -> {out_dir}/")

    ok = fail = 0
    for f in sorted(files):
        if convert_texture(f, out_dir, palette, lut, args, src_palette):
            ok += 1
        else:
            fail += 1

    print(f"\n-- Done: {ok} converted, {fail} failed --")
    if ok:
        print(f"   Output: {out_dir.resolve()}")


if __name__ == '__main__':
    main()