"""Cyrillic Notes clause 3: preserve the existing ASCII layout and pixels."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image


def generate(directory):
    root = Path(directory)
    (root / "original.tgf").write_text("HEIGHT 2;\nGLYPH 1 1 2 -1 2;\t// fixture\nGLYPH_U32 1105 0 0 1 0 0;\n")
    (root / "extra.tgf").write_text("HEIGHT 2;\nGLYPH 0 0 3 0 0;\nGLYPH_U32 1040 1 1 2 0 1;\nGLYPH_U32 1105 2 2 1 1 1;\n")
    Image.new("RGBA", (4, 4), (97, 29, 11, 191)).save(root / "original.2.tga")
    Image.new("RGBA", (4, 4), (13, 41, 83, 157)).save(root / "extra.2.tga")
    subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "extend_font.py"),
                    str(root / "original.tgf"), str(root / "extra.tgf"), str(root / "output.tgf")], check=True, timeout=30)
    return root


class ExtensionTests(unittest.TestCase):
    """Check generated assets through the command-line interface."""

    def test_preserves_original_glyph_metrics_and_coordinates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = generate(directory)
            self.assertTrue((root / "output.tgf").read_text().startswith((root / "original.tgf").read_text()))

    def test_preserves_original_atlas_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            root = generate(directory)
            with Image.open(root / "output.2.tga") as atlas, Image.open(root / "original.2.tga") as original:
                self.assertEqual(original.tobytes(), atlas.crop((0, 0, 4, 4)).tobytes())

    def test_offsets_new_glyph_coordinates_below_original_atlas(self):
        with tempfile.TemporaryDirectory() as directory:
            root = generate(directory)
            self.assertIn("GLYPH_U32 1040 1 5 2 0 1;", (root / "output.tgf").read_text())

    def test_preserves_new_glyph_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            root = generate(directory)
            with Image.open(root / "output.2.tga") as atlas, Image.open(root / "extra.2.tga") as extra:
                self.assertEqual(extra.crop((1, 1, 3, 3)).tobytes(), atlas.crop((1, 5, 3, 7)).tobytes())

    # Notes repair clause 3: extending again must preserve existing Unicode too.
    def test_does_not_override_existing_unicode_glyphs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = generate(directory)
            self.assertEqual(1, (root / "output.tgf").read_text().count("GLYPH_U32 1105 "))


if __name__ == "__main__":
    unittest.main()
