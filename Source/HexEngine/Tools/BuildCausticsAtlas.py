"""Builds the underwater caustics atlas consumed by Deferred.shader (t13).

Source : <Bin>/x64/<Config>/Data/Textures/caustics/caust00.png .. caust31.png
         32 greyscale 256x256 frames - a seamless-tiling, perfectly looping
         caustic animation.
Output : <Bin>/x64/<Config>/Data/Textures/caustics/caustics_atlas.png
         2080 x 1040, 8 columns x 4 rows of 260 px cells.

Each 256 px tile is stored with a 2 px WRAPPED gutter. The shader tiles a frame
by taking frac() of its UV and sampling inside the cell; without the gutter,
bilinear filtering at the tile edge would blend in the NEIGHBOURING FRAME of the
atlas (a visible seam line every tile). Wrapping the gutter makes the filter see
exactly what a real wrap-addressed texture would.

The layout constants are mirrored in Deferred.shader (kCausticAtlasPx,
kCausticCellPx, kCausticTilePx, kCausticGutterPx, kCausticFrames) - change both.

The Bin data tree is git-ignored, so this script is the source of truth for the
atlas. Requires Pillow + numpy.

    python BuildCausticsAtlas.py [repo_root]
"""
import os
import sys

import numpy as np
from PIL import Image

TILE = 256
GUTTER = 2
COLS, ROWS = 8, 4
FRAMES = COLS * ROWS
CELL = TILE + 2 * GUTTER


def build(src_dir: str) -> Image.Image:
    atlas = np.zeros((ROWS * CELL, COLS * CELL), dtype=np.uint8)
    for i in range(FRAMES):
        frame = Image.open(os.path.join(src_dir, f"caust{i:02d}.png")).convert("L")
        if frame.size != (TILE, TILE):
            raise ValueError(f"caust{i:02d}.png is {frame.size}, expected {TILE}x{TILE}")
        padded = np.pad(np.asarray(frame), GUTTER, mode="wrap")
        row, col = divmod(i, COLS)
        atlas[row * CELL:(row + 1) * CELL, col * CELL:(col + 1) * CELL] = padded
    # RGB so the engine's texture importer takes its ordinary 8-bit path.
    return Image.fromarray(atlas, "L").convert("RGB")


def main() -> int:
    repo = sys.argv[1] if len(sys.argv) > 1 else os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    wrote = 0
    for config in ("Debug", "Release"):
        directory = os.path.join(repo, "Bin", "x64", config, "Data", "Textures", "caustics")
        if not os.path.isfile(os.path.join(directory, "caust00.png")):
            print(f"skip {config}: no source frames in {directory}")
            continue
        out = os.path.join(directory, "caustics_atlas.png")
        build(directory).save(out)
        print(f"wrote {out}")
        wrote += 1
    return 0 if wrote else 1


if __name__ == "__main__":
    sys.exit(main())
