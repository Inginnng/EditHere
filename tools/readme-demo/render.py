"""Turns the frames written by readme_recorder into the README GIFs.

    python tools/readme-demo/render.py <frames folder> [--width 960]

Each <scene>-<lang>/timeline.json becomes assets/readme/<scene>-<lang>.gif. The
palette is built per scene from a sample of its frames so UI colours stay exact,
and frames are resampled to a steady rate so long holds cost one frame each.
"""
import argparse
import json
import pathlib

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "assets" / "readme"


def load(folder: pathlib.Path, width: int):
    timeline = json.loads((folder / "timeline.json").read_text(encoding="utf-8"))
    frames, durations = [], []
    for entry in timeline["frames"]:
        image = Image.open(folder / entry["file"]).convert("RGB")
        if image.width != width:
            image = image.resize((width, round(image.height * width / image.width)), Image.LANCZOS)
        ms = max(20, int(entry["ms"]))
        # GIF delays are in 10 ms steps; merge very short frames into the next one.
        if frames and durations[-1] < 40:
            frames[-1] = image
            durations[-1] += ms
        else:
            frames.append(image)
            durations.append(ms)
    return frames, durations


def palette(frames):
    sample = frames[:: max(1, len(frames) // 24)]
    strip = Image.new("RGB", (sample[0].width, sample[0].height * len(sample)))
    for i, frame in enumerate(sample):
        strip.paste(frame, (0, i * frame.height))
    return strip.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)


def render(folder: pathlib.Path, width: int):
    frames, durations = load(folder, width)
    reference = palette(frames)
    quantized = [frame.quantize(palette=reference, dither=Image.Dither.NONE) for frame in frames]
    target = OUT / f"{folder.name}.gif"
    quantized[0].save(target, save_all=True, append_images=quantized[1:], duration=durations,
                      loop=0, optimize=True, disposal=1)
    print(f"{target.relative_to(ROOT)}: {len(frames)} frames, {sum(durations) / 1000:.1f} s, "
          f"{target.stat().st_size / 1e6:.1f} MB")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("frames", type=pathlib.Path)
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    for folder in sorted(p for p in args.frames.iterdir() if (p / "timeline.json").is_file()):
        if not args.only or folder.name in args.only:
            render(folder, args.width)


if __name__ == "__main__":
    main()
