# Path of Building Font Generator

This utility builds bitmap fonts used by the Path of Building Community fork. It wraps Windows GDI font rendering into a standalone Win32 application that packs glyphs into `.tga` atlases and emits metadata consumed by the main tool.

## Prerequisites
- Visual Studio 2022 (v143 toolset) with the Desktop development with C++ workload
- Windows 10 SDK (10.0 or newer)

## Building
1. Open `GLFontGen.sln` in Visual Studio.
2. Select the desired configuration (`Release` is recommended for distributing fonts).
3. Build the solution (`Build > Build Solution`).

The resulting executable (`GLFontGen.exe`) stays local to your build environment; it is intentionally not checked into source control.

## Usage
1. Run `GLFontGen.exe`.
2. Pick a font face, weight, and whether to force fixed pitch.
3. Select the font sizes you want to export.
4. Click **Generate**.

Outputs are written to `Generated Fonts/` beside the executable:

- One `.tga` atlas per selected point size (e.g. `Consolas.16.tga`).
- A single `.tgf` metadata file with layout information for every generated height.

## Output Format
- **TGA**: 32-bit BGRA image containing packed glyphs with premultiplied alpha. Dimensions are powers of two chosen to fit the selected glyph set.
- **TGF metadata**:
  - Each font size starts with `HEIGHT <px>;`.
  - ASCII entries use `GLYPH <x> <y> <width> <left> <right>` with optional ASCII comment.
  - Available Cyrillic glyphs (U+0400 through U+04FF) use `GLYPH_U32 <codepoint> <x> <y> <width> <left> <right>;`, with a decimal codepoint.
  - `x`/`y` are the glyph origin inside the atlas, `width` is the advance width in pixels, and `left`/`right` provide side bearings.

Refer to `fontgen.cpp` if you need deeper details or want to extend the format.

## Extending an existing atlas

Regenerating a font can change the original ASCII metrics or pixels when the
installed font or rasterizer differs. To preserve an existing atlas, generate
the extra glyphs at the same heights, then append them with Python and Pillow:

```text
python -m pip install Pillow
python extend_font.py "original/Liberation Sans.tgf" "Generated Fonts/Liberation Sans.tgf" "extended/Liberation Sans.tgf"
```

The output directory receives both the extended TGF and its TGA files. Original
glyph entries and atlas pixels stay unchanged; Unicode entries refer to the
appended portion. Use a separate output directory. The engine must understand
`GLYPH_U32` to display the new glyphs. Font redistribution still requires the
font's license; the generator's license does not replace it.

## Regression checks

Build and run `tests/fontgen_test.cpp` from an x86 Visual Studio developer shell
in a scratch working directory. It includes the generator implementation and
requires `user32.lib`, `gdi32.lib`, and `comctl32.lib` when linking.
It writes disposable font assets and checks four representative Cyrillic glyphs.

```text
python -m unittest discover -s tests -p test_extend_font.py
```

The Python tests verify preservation of original pixels and metadata and the
coordinates and pixels of appended glyphs.

## License
Released under the MIT License (see `LICENSE`).
