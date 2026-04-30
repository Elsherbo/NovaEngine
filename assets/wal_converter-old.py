"""
RedEye Engine — WAL Texture Converter
======================================
Converts PNG/JPG/BMP/TGA images into Quake2-style .wal files
palettized against your colormap.pcx (raw 768-byte palette).

Usage:
    python wal_converter.py --palette colormap.pcx --input textures/ --output wal_out/

Optional flags:
    --preview          Save a side-by-side preview PNG for each texture
    --dither           Use Floyd-Steinberg dithering (better gradients, slower)
    --no-mips          Skip mip level generation (mip 0 only)
    --flags 0          WAL surface flags (default 0)
    --contents 0       WAL contents flags (default 0)

Requirements:
    pip install Pillow numpy
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
except ImportError:
    print("[ERROR] Missing dependencies. Run:  pip install Pillow numpy")
    sys.exit(1)


# ── WAL format constants ──────────────────────────────────────────────────────
WAL_HEADER_SIZE = 100   # bytes
WAL_MIP_LEVELS  = 4
WAL_NAME_LEN    = 32


# ─────────────────────────────────────────────────────────────────────────────
# Palette loading
# ─────────────────────────────────────────────────────────────────────────────

def load_palette(path: str) -> np.ndarray:
    """
    Load a raw 768-byte palette file (256 × RGB).
    Returns shape (256, 3) uint8 array.
    """
    data = Path(path).read_bytes()
    if len(data) != 768:
        raise ValueError(f"Expected 768-byte palette, got {len(data)} bytes: {path}")
    pal = np.frombuffer(data, dtype=np.uint8).reshape(256, 3).copy()
    return pal


# ─────────────────────────────────────────────────────────────────────────────
# Color matching — nearest palette entry in perceptual RGB space
# ─────────────────────────────────────────────────────────────────────────────

# Precompute perceptual weights (Rec. 601 luma coefficients)
# gives better results than raw Euclidean distance
_W = np.array([0.299, 0.587, 0.114], dtype=np.float32)


def build_lookup_table(palette: np.ndarray) -> np.ndarray:
    """
    Precompute a 16-bit lookup table: for every possible (r>>3, g>>3, b>>3)
    quantized color, store the nearest palette index.
    This turns O(256) per-pixel search into O(1) lookup after a one-time build.

    Table shape: (32, 32, 32) → uint8 palette index
    """
    print("  Building color lookup table...", end=" ", flush=True)
    t0 = time.time()

    # All 32768 quantized RGB values
    rv = np.arange(32, dtype=np.float32) * 8 + 4   # midpoints
    gv = np.arange(32, dtype=np.float32) * 8 + 4
    bv = np.arange(32, dtype=np.float32) * 8 + 4

    R, G, B = np.meshgrid(rv, gv, bv, indexing='ij')  # (32,32,32) each
    queries = np.stack([R, G, B], axis=-1)             # (32,32,32,3)

    pal_f = palette.astype(np.float32)                 # (256, 3)

    # Vectorised weighted Euclidean: diff shape → (32,32,32,256,3)
    # To avoid OOM we chunk over the 32*32=1024 RG pairs
    lut = np.zeros((32, 32, 32), dtype=np.uint8)
    for ri in range(32):
        for gi in range(32):
            row = queries[ri, gi]          # (32, 3)
            diff = row[:, None, :] - pal_f[None, :, :]  # (32, 256, 3)
            dist = (diff ** 2 * _W).sum(axis=-1)        # (32, 256)
            lut[ri, gi] = dist.argmin(axis=-1).astype(np.uint8)

    print(f"done ({time.time()-t0:.2f}s)")
    return lut


def palettize(img_rgb: np.ndarray, lut: np.ndarray,
              dither: bool = False, palette: np.ndarray = None) -> np.ndarray:
    """
    Convert an HxW RGB image to HxW palette indices using the LUT.
    Optionally applies Floyd-Steinberg dithering.
    """
    h, w = img_rgb.shape[:2]
    img = img_rgb.astype(np.float32)

    if dither and palette is not None:
        # Floyd-Steinberg dithering — process row by row
        result = np.zeros((h, w), dtype=np.uint8)
        pal_f = palette.astype(np.float32)

        for y in range(h):
            for x in range(w):
                old = img[y, x].clip(0, 255)
                # LUT lookup
                ri = int(old[0]) >> 3
                gi = int(old[1]) >> 3
                bi = int(old[2]) >> 3
                idx = lut[ri, gi, bi]
                result[y, x] = idx
                # Error diffusion
                err = old - pal_f[idx]
                if x + 1 < w:
                    img[y, x+1]     += err * (7/16)
                if y + 1 < h:
                    if x > 0:
                        img[y+1, x-1] += err * (3/16)
                    img[y+1, x]       += err * (5/16)
                    if x + 1 < w:
                        img[y+1, x+1] += err * (1/16)
        return result
    else:
        # Fast vectorised nearest-neighbor (no dithering)
        qi = (img[..., 0].astype(np.int32) >> 3).clip(0, 31)
        qg = (img[..., 1].astype(np.int32) >> 3).clip(0, 31)
        qb = (img[..., 2].astype(np.int32) >> 3).clip(0, 31)
        return lut[qi, qg, qb]


# ─────────────────────────────────────────────────────────────────────────────
# Mip level generation
# ─────────────────────────────────────────────────────────────────────────────

def make_mips(indexed: np.ndarray, palette: np.ndarray,
              lut: np.ndarray, n_mips: int = 4) -> list[np.ndarray]:
    """
    Generate n_mips mip levels. Each level is half the previous.
    Downsample in RGB space (better quality) then re-palettize.
    """
    mips = [indexed]
    h, w = indexed.shape

    # Reconstruct RGB for high-quality downsampling
    rgb = palette[indexed]  # (h, w, 3) uint8

    for _ in range(n_mips - 1):
        h, w = h // 2, w // 2
        if h < 1 or w < 1:
            break
        pil = Image.fromarray(rgb).resize((w, h), Image.LANCZOS)
        rgb = np.array(pil)
        mips.append(palettize(rgb, lut))

    return mips


# ─────────────────────────────────────────────────────────────────────────────
# WAL writer
# ─────────────────────────────────────────────────────────────────────────────

def write_wal(output_path: str, name: str, mips: list[np.ndarray],
              flags: int = 0, contents: int = 0, value: int = 0) -> None:
    """
    Write a Quake2 .wal file with the given mip levels (palette index data).

    WAL header layout (100 bytes):
        char[32]  name
        uint32    width
        uint32    height
        uint32[4] offsets   (from file start, one per mip)
        char[32]  next_name (unused, zeroed)
        uint32    flags
        uint32    contents
        uint32    value
    """
    h0, w0 = mips[0].shape
    n_mips  = min(len(mips), WAL_MIP_LEVELS)

    # Compute offsets
    offsets = []
    current = WAL_HEADER_SIZE
    for m in mips[:n_mips]:
        offsets.append(current)
        current += m.size
    # Pad to 4 offsets
    while len(offsets) < WAL_MIP_LEVELS:
        offsets.append(current)

    # Encode name (null-padded to 32 bytes)
    name_bytes = name.encode('ascii', errors='replace')[:WAL_NAME_LEN - 1]
    name_bytes = name_bytes.ljust(WAL_NAME_LEN, b'\x00')

    header = struct.pack('<32sII4I32sIII',
        name_bytes,
        w0, h0,
        *offsets[:4],
        b'\x00' * WAL_NAME_LEN,
        flags,
        contents,
        value,
    )
    assert len(header) == WAL_HEADER_SIZE, f"Header size mismatch: {len(header)}"

    with open(output_path, 'wb') as f:
        f.write(header)
        for m in mips[:n_mips]:
            f.write(m.flatten().tobytes())


# ─────────────────────────────────────────────────────────────────────────────
# Preview generation
# ─────────────────────────────────────────────────────────────────────────────

def save_preview(src_rgb: np.ndarray, indexed: np.ndarray,
                 palette: np.ndarray, output_path: str) -> None:
    """Save a side-by-side comparison: original | palettized | 4 mip levels."""
    h, w = src_rgb.shape[:2]
    pal_rgb = palette[indexed]

    # Mip thumbnails for display
    images = [src_rgb, pal_rgb]
    mh, mw = h // 2, w // 2
    if mh > 0 and mw > 0:
        images.append(np.array(Image.fromarray(pal_rgb).resize((mw, mh), Image.NEAREST)))

    # Stitch horizontally (pad shorter images vertically)
    max_h = max(img.shape[0] for img in images)
    strips = []
    for img in images:
        pad = np.zeros((max_h, img.shape[1], 3), dtype=np.uint8)
        pad[:img.shape[0]] = img
        strips.append(pad)

    combined = np.concatenate(strips, axis=1)
    Image.fromarray(combined).save(output_path)


# ─────────────────────────────────────────────────────────────────────────────
# Dimension helpers
# ─────────────────────────────────────────────────────────────────────────────

def next_power_of_two(n: int) -> int:
    p = 1
    while p < n:
        p <<= 1
    return p

def fit_to_wal(img: Image.Image) -> Image.Image:
    """
    WAL textures must be multiples of 8 (ideally powers of 2).
    Resize if needed — warn the user if dimensions changed.
    """
    w, h = img.size
    nw = max(8, next_power_of_two(w))
    nh = max(8, next_power_of_two(h))
    if nw != w or nh != h:
        print(f"    [warn] Resized {w}×{h} → {nw}×{nh} (WAL requires power-of-2)")
        img = img.resize((nw, nh), Image.LANCZOS)
    return img


# ─────────────────────────────────────────────────────────────────────────────
# Main conversion pipeline
# ─────────────────────────────────────────────────────────────────────────────

SUPPORTED = {'.png', '.jpg', '.jpeg', '.bmp', '.tga', '.tiff', '.webp'}

def convert_texture(src_path: Path, out_dir: Path, palette: np.ndarray,
                    lut: np.ndarray, args) -> bool:
    try:
        img = Image.open(src_path).convert('RGB')
        img = fit_to_wal(img)
        rgb = np.array(img)

        indexed = palettize(rgb, lut,
                            dither=args.dither,
                            palette=palette if args.dither else None)

        mips = make_mips(indexed, palette, lut) if not args.no_mips else [indexed]

        tex_name = src_path.stem
        wal_path = out_dir / (tex_name + '.wal')
        write_wal(str(wal_path), tex_name, mips,
                  flags=args.flags, contents=args.contents)

        if args.preview:
            prev_path = out_dir / (tex_name + '_preview.png')
            save_preview(rgb, indexed, palette, str(prev_path))

        w, h = img.size
        mip_str = f"({len(mips)} mips)" if not args.no_mips else "(no mips)"
        size_kb = wal_path.stat().st_size / 1024
        print(f"    ✓  {tex_name}.wal  {w}×{h}  {mip_str}  {size_kb:.1f} KB")
        return True

    except Exception as e:
        print(f"    ✗  {src_path.name}: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(
        description="RedEye Engine WAL Texture Converter",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument('--palette',   required=True,   help='Path to colormap.pcx (768 bytes)')
    parser.add_argument('--input',     required=True,   help='Source image or folder')
    parser.add_argument('--output',    default='wal_out', help='Output folder (default: wal_out/)')
    parser.add_argument('--preview',   action='store_true', help='Save before/after preview PNGs')
    parser.add_argument('--dither',    action='store_true', help='Floyd-Steinberg dithering')
    parser.add_argument('--no-mips',   action='store_true', help='Skip mip levels')
    parser.add_argument('--flags',     type=int, default=0, help='WAL surface flags')
    parser.add_argument('--contents',  type=int, default=0, help='WAL contents flags')
    args = parser.parse_args()

    # ── Load palette
    print(f"\n[1/4] Loading palette: {args.palette}")
    try:
        palette = load_palette(args.palette)
        print(f"      256 colors loaded ✓")
    except Exception as e:
        print(f"[ERROR] {e}")
        sys.exit(1)

    # ── Build LUT
    print(f"[2/4] Building color LUT...")
    lut = build_lookup_table(palette)

    # ── Collect source files
    print(f"[3/4] Scanning input: {args.input}")
    src = Path(args.input)
    if src.is_file():
        files = [src] if src.suffix.lower() in SUPPORTED else []
    else:
        files = [f for f in src.rglob('*') if f.suffix.lower() in SUPPORTED]

    if not files:
        print(f"[ERROR] No supported images found ({', '.join(SUPPORTED)})")
        sys.exit(1)
    print(f"      Found {len(files)} texture(s)")

    # ── Convert
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"[4/4] Converting → {out_dir}/")

    ok = fail = 0
    for f in sorted(files):
        if convert_texture(f, out_dir, palette, lut, args):
            ok += 1
        else:
            fail += 1

    print(f"\n── Done: {ok} converted, {fail} failed ──")
    if ok:
        print(f"   WAL files ready in:  {out_dir.resolve()}")
        print(f"   Point TrenchBroom game path to that folder.")


if __name__ == '__main__':
    main()
