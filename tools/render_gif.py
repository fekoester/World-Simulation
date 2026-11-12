"""Turn the PPM frames written by `wsim --dump-dir DIR` into an animated GIF."""

import argparse
import glob
import os

from PIL import Image


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("frames", help="directory with frame_*.ppm")
    ap.add_argument("--out", default="docs/world.gif")
    ap.add_argument("--fps", type=float, default=12)
    ap.add_argument("--width", type=int, default=0, help="resize to this width (0 = keep)")
    args = ap.parse_args()

    paths = sorted(glob.glob(os.path.join(args.frames, "frame_*.ppm")))
    if not paths:
        raise SystemExit(f"no frames in {args.frames}")
    frames = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        if args.width:
            im = im.resize((args.width, round(im.height * args.width / im.width)), Image.LANCZOS)
        frames.append(im.quantize(colors=64, method=Image.Quantize.MEDIANCUT))
    frames[0].save(args.out, save_all=True, append_images=frames[1:], duration=int(1000 / args.fps), loop=0)
    print(f"wrote {args.out} ({len(frames)} frames)")


if __name__ == "__main__":
    main()
