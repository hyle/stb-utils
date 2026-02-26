# stb-utils

Ultra-fast, zero-dependency command-line tools built entirely on
[nothings/stb](https://github.com/nothings/stb) single-file public domain libraries.

No libpng. No freetype. No libjpeg. One static binary, runs anywhere.

---

## Tools

### `stb-noise`

Generate Perlin noise images for procedural textures, heightmaps, and
placeholder assets in automated pipelines.

```sh
stb-noise [WxH] [scale] [output.png] [--octaves N]
```

| Argument | Default | Description |
|---|---|---|
| `WxH` | `512x512` | Output dimensions |
| `scale` | `4.0` | Noise frequency (higher = finer detail) |
| `output.png` | `noise.png` | Output file |
| `--octaves N` | `1` | FBM octaves (`1-32`) |

**Examples:**

```sh
# Default 512x512, scale 4
stb-noise

# 256x256 heightmap with fine detail
stb-noise 256x256 8.0 heightmap.png

# Tiny tiling texture
stb-noise 64x64 2.0 tile.png

# FBM noise with 6 octaves
stb-noise 512x512 4.0 fbm.png --octaves 6
```

### `stb-atlas`

Generate a font texture atlas from a TrueType font, with glyph UV
coordinates and metrics output as JSON for use in a game engine or UI renderer.

```sh
stb-atlas [font.ttf] [size] [output.png]
```

| Argument | Default | Description |
|---|---|---|
| `font.ttf` | *(required)* | Path to a TrueType or OpenType font |
| `size` | `32.0` | Font size in pixels |
| `output.png` | `atlas.png` | Output atlas image (1-channel grayscale) |

JSON metadata is printed to stdout. Redirect it to a file alongside the PNG.

**Examples:**

```sh
# Default 32px atlas
stb-atlas font.ttf

# 16px pixel font, explicit output paths
stb-atlas fonts/proggy_clean.ttf 16 ui_font.png > ui_font.json

# Large atlas for high-DPI UI
stb-atlas fonts/inter.ttf 48 hud.png > hud.json
```

**Output format:**

```json
{
  "texture": "ui_font.png",
  "width": 512,
  "height": 512,
  "size": 16.0,
  "glyphs": {
    "65": { "char": "A", "x": 14, "y": 0, "w": 8, "h": 13,
            "xoff": 0.00, "yoff": -12.00, "xadvance": 7.00 }
  }
}
```

| Field | Description |
|---|---|
| `x`, `y` | Top-left pixel coordinate in the atlas texture |
| `w`, `h` | Glyph bounding box in pixels |
| `xoff`, `yoff` | Offset from cursor position to glyph top-left (bearing) |
| `xadvance` | Horizontal advance to move the cursor after drawing |

Covers ASCII 32-126 (space through `~`). The atlas is a single-channel
PNG; sample it with `r` or replicate to RGB in your shader.

---

## Install

### Download a binary

Grab a pre-built static binary from
[Releases](https://github.com/hyle/stb-utils/releases).
No runtime dependencies, just download and run.

```sh
# Linux x86_64
curl -Lo stb-noise https://github.com/hyle/stb-utils/releases/latest/download/stb-noise-linux-x86_64
chmod +x stb-noise
./stb-noise --help
```

### Build from source

Requires `clang` or `gcc` and `make`.

```sh
git clone https://github.com/hyle/stb-utils
cd stb-utils
make
./build/stb-noise --help
```

**Static Linux binary via Docker (Alpine/musl):**

```sh
docker build -f Dockerfile.build -t stb-utils-builder .
docker run --rm -v $(pwd)/dist:/src/build stb-utils-builder
# dist/stb-noise is fully static, ldd reports "not a dynamic executable"
```

---

## Why

Tools like ImageMagick are powerful but heavy, pulling them into a minimal
CI/CD container adds hundreds of megabytes and dozens of transitive
dependencies. `stb-utils` is the opposite: a single small executable that
does one thing, built on Sean Barrett's single-header libraries.

Designed for:
- Game dev asset pipelines (procedural textures, atlases)
- CI/CD containers where `apt install imagemagick` is not an option
- Shell scripting and UNIX pipelines
- Anywhere you need image generation with zero install friction

**Format support:** PNG, BMP, TGA, JPEG, HDR (what stb supports, no WebP,
no AVIF, no TIFF, by design).

---

## Development

```sh
make              # build (native, dynamic, fast iteration)
make test         # run test suite
make sanitize     # build with ASan + UBSan for safety checks
make clean
```

Tests live in `tests/test_cli.sh` and `tests/test_atlas.sh`.

stb headers are vendored in `vendor/stb/` at commit
[`f1c79c0`](https://github.com/nothings/stb/commit/f1c79c02822848a9bed4315b12c8c8f3761e1296).

---

## Roadmap

- [x] `stb-noise` FBM mode (`--octaves N`)
- [x] `stb-atlas` generate font texture atlases from TTF files - outputs JSON metadata to stdout
- [ ] `stb-img` convert and resize images
- [ ] UNIX pipeline mode (`stdin -> stdout`)

---

## License

MIT. stb headers are public domain (or MIT, at your option),
see `vendor/stb/` headers for details.
