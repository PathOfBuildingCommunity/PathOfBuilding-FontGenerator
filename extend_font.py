"""Append generated Unicode glyphs without replacing an existing ASCII atlas."""
import argparse
from pathlib import Path
import re

from PIL import Image


def sections(path):
    result = {}
    height = None
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"HEIGHT (\d+);", line)
        if match:
            height = int(match[1])
            result[height] = []
        elif height is not None:
            result[height].append(line)
    return result


def extend(original, extra, output):
    base = sections(original)
    additions = sections(extra)
    if base.keys() != additions.keys():
        raise ValueError("Font heights must match")
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = []
    for height, glyphs in base.items():
        with Image.open(original.with_suffix(f".{height}.tga")) as before, Image.open(extra.with_suffix(f".{height}.tga")) as after:
            width = max(before.width, after.width)
            size = 1 << (before.height + after.height - 1).bit_length()
            atlas = Image.new("RGBA", (width, size), (255, 255, 255, 0))
            atlas.paste(before, (0, 0))
            atlas.paste(after, (0, before.height))
            lines.extend([f"HEIGHT {height};", *glyphs])
            existing = {int(glyph.split()[1]) for glyph in glyphs if glyph.startswith("GLYPH_U32 ")}
            for glyph in additions[height]:
                match = re.match(r"GLYPH_U32 (\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+(-?\d+);", glyph)
                if match:
                    codepoint, x, y, width, left, right = map(int, match.groups())
                    if codepoint not in existing:
                        lines.append(f"GLYPH_U32 {codepoint} {x} {y + before.height} {width} {left} {right};")
            atlas.save(output.with_suffix(f".{height}.tga"), compression="tga_rle")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("extra", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    extend(arguments.original, arguments.extra, arguments.output)
