from __future__ import annotations

import argparse
from pathlib import Path
from typing import NamedTuple

from PIL import Image


class TrimResult(NamedTuple):
    path: Path
    original_size: tuple[int, int]
    trimmed_size: tuple[int, int]
    bbox: tuple[int, int, int, int] | None


def inspect_png(path: Path) -> TrimResult:
    with Image.open(path) as image:
        rgba = image.convert("RGBA")
        alpha = rgba.getchannel("A")
        bbox = alpha.getbbox()

        original_size = rgba.size
        if bbox is None:
            return TrimResult(path, original_size, original_size, None)

        trimmed = rgba.crop(bbox)
        return TrimResult(path, original_size, trimmed.size, bbox)


def trim_and_pad_png(path: Path, canvas_size: tuple[int, int]) -> tuple[bool, tuple[int, int], tuple[int, int]]:
    with Image.open(path) as image:
        rgba = image.convert("RGBA")
        alpha = rgba.getchannel("A")
        bbox = alpha.getbbox()

        original_size = rgba.size
        if bbox is None:
            trimmed = Image.new("RGBA", canvas_size, (0, 0, 0, 0))
        else:
            cropped = rgba.crop(bbox)
            trimmed = Image.new("RGBA", canvas_size, (0, 0, 0, 0))
            x_offset = (canvas_size[0] - cropped.width) // 2
            y_offset = canvas_size[1] - cropped.height
            trimmed.paste(cropped, (x_offset, y_offset))

        changed = original_size != canvas_size or bbox is not None and bbox != (0, 0, original_size[0], original_size[1])
        trimmed.save(path)
        return changed, original_size, canvas_size


def iter_pngs(folder: Path) -> list[Path]:
    return sorted(path for path in folder.iterdir() if path.is_file() and path.suffix.lower() == ".png")


def main() -> int:
    parser = argparse.ArgumentParser(description="Trim transparent padding from PNG files in-place.")
    parser.add_argument("folders", nargs="+", help="Folder paths containing PNG files")
    args = parser.parse_args()

    total_files = 0
    changed_files = 0

    all_pngs: list[Path] = []
    for folder_arg in args.folders:
        folder = Path(folder_arg)
        if not folder.is_dir():
            print(f"Skipping missing folder: {folder}")
            continue

        pngs = iter_pngs(folder)
        all_pngs.extend(pngs)
        print(f"{folder}: {len(pngs)} PNG files")

    inspected = [inspect_png(path) for path in all_pngs]
    if not inspected:
        print("done: changed 0 of 0 PNG files")
        return 0

    canvas_width = max(result.trimmed_size[0] for result in inspected)
    canvas_height = max(result.trimmed_size[1] for result in inspected)
    canvas_size = (canvas_width, canvas_height)
    print(f"uniform canvas size: {canvas_size}")

    for folder_arg in args.folders:
        folder = Path(folder_arg)
        if not folder.is_dir():
            continue

        pngs = iter_pngs(folder)
        folder_changed = 0
        for png in pngs:
            total_files += 1
            changed, original_size, trimmed_size = trim_and_pad_png(png, canvas_size)
            if changed:
                changed_files += 1
                folder_changed += 1
                print(f"trimmed {png.name}: {original_size} -> {trimmed_size}")

        print(f"changed {folder_changed} files in {folder}")

    print(f"done: changed {changed_files} of {total_files} PNG files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
