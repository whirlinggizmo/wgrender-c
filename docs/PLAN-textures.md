# Plan: compressed textures

Status: built (2026-09-19).

## Why

A texture loaded from a PNG costs, every time it loads: decoding the PNG, building
its mipmaps on the CPU, and 4 bytes a pixel of GPU memory (a third more with
mipmaps). Phones feel all three: less memory, slower CPUs. GPUs sample compressed
formats directly at 1 byte a pixel, with the mipmaps made ahead of time.

## The choice: files per GPU family, not one file transcoded

GPUs don't share a compressed format: desktops have BC7, phones ASTC and ETC2.
Basis Universal stores one file and transcodes it on the device, but its transcoder
(C++) weighed, compiled for the web with only the formats libsk needs:

| transcoder (web)                     | wasm   | gzipped |
|--------------------------------------|--------|---------|
| current release (with HDR)           | 883 KB | 393 KB  |
| 1.16.4 (before HDR), our formats     | 404 KB | 197 KB  |
| 1.16.4, only BC7 + ASTC + RGBA       | 268 KB | 126 KB  |

For comparison, all of `hello` is 134 KB gzipped. So libsk ships files already in
each family's format instead and loads the one the GPU can use: no transcoder, a few
KB of C to read the files, and no work at load but reading and uploading.

## How it's built

- **Files:** `tools/compress_textures.sh name.png` writes `name.bc7.ktx` (BC7),
  `name.astc.ktx` (ASTC 4x4) and `name.etc2.ktx` (ETC2 RGBA) beside it: KTX 1 files,
  16 bytes a 4x4 block, with the full mipmap chain. It encodes with Basis Universal
  (UASTC level 2, then transcoded to each format), built the first time from a pinned
  release (1.16.4) into `build/tools`: a tool for making assets, never linked in.
  `--linear` for data textures (normal maps, roughness).
- **Names:** a program loads `name.ktx`, through `sk_asset` or `sk_texture_create`.
  The texture module maps it to the first variant the GPU can sample, in order BC7,
  ASTC, ETC2, else `name.png`: the asset layer maps it before fetching
  (`sk_asset_register_path_mapper`), so the web downloads and caches only that file
  and the callback gets its path, and `sk_texture_create` maps it the same way. A
  variant named outright (`name.astc.ktx`) is used as is.
- **Loading:** a second loader in the texture module (`.ktx`) reads and checks the file
  on the asset workers (`src/sk_ktx.c`: KTX 1, little-endian, 2D, one of the three
  formats, every level's size matching its dimensions) and uploads the levels as they
  are. The formats are the plain (not sRGB) ones, like RGBA8 textures: the shaders
  treat texture colors as sRGB values either way. The texture module links it: a
  program without textures pays nothing.
- **Picking:** pixel-accurate sprite picking (`set_pick_alpha_test`) reads the PNG
  beside a compressed texture, as it reads a PNG texture's own file.

## Measured

A 2048x2048 texture (FlightHelmet's leather base color: a 5.5 MB PNG; each compressed
file 5.6 MB with its mipmaps), created synchronously, three times each:

|                           | PNG           | compressed          |
|---------------------------|---------------|---------------------|
| desktop GL (NVIDIA), BC7  | 59-89 ms      | 0.8-1.0 ms          |
| Pixel 9 (WebGL2), ASTC    | 125-203 ms    | 1.4-1.9 ms          |
| GPU memory with mipmaps   | 21.3 MB       | 5.3 MB              |

Download size depends on the image: a detailed 2K texture's compressed file was about
its PNG's size; small, simple images compress far better as PNGs (a 256x256 logo:
13.5 KB PNG, 87.5 KB each variant), though HTTP compression brings the variants down
(the 256x256 flame: 28 KB PNG, 22 KB for BC7 gzipped).

Checked on desktop GL, WebGL2 (SwiftShader), WebGPU (NVIDIA; BC7 through its
`texture-compression-bc` feature), the Pixel's WebGL2 (ASTC), and the Windows build
under Wine (BC7). `examples/textures.c` shows each texture as PNG and compressed.

## Not in this plan

- Smaller ASTC blocks (6x6, 8x8: 3.6 and 2 bits a pixel) for smaller downloads, at
  some quality.
- glTF models' textures (their images stay PNG/JPEG); KHR_texture_basisu.
- KTX 2 and its zstd supercompression; Basis transcoding as a second, optional module
  if single-file assets turn out to matter.
- sRGB formats (with sRGB-correct filtering), when the renderer moves to linear
  sampling.
