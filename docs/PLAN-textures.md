# Plan: compressed textures

Status: built (2026-09-19), for textures and for glTF models' textures.

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
- **Missing files:** when this GPU's variant is missing (compressed for some formats,
  or not at all), `name.png` loads instead, with a warning naming the missing file:
  the asset layer retries the PNG once the variant's fetch fails (a 404 on the web; a
  task's fallback path, from the path mapper), and `sk_texture_create` checks for the
  variant before loading. A variant named outright has no fallback.
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

## glTF models

- `tools/compress_textures.sh --gltf model.gltf` (`tools/compress_gltf.py`) compresses
  every image the model's textures use and writes `model.ktx.gltf` beside the model,
  leaving it as it was. Data textures (normal, metallic-roughness and occlusion maps)
  are compressed as linear, colors (base color, emissive) as sRGB. Each texture gets
  the `SK_texture_ktx` extension, `{"source": <image>}`, pointing at an added
  `name.ktx` image; the texture keeps its own image as `source`. The extension is in
  `extensionsUsed`, not `extensionsRequired`: other viewers ignore it and use the
  original images, so the file stays a portable glTF. Only `.gltf` files with images
  in separate PNG or JPEG files: images inside the file (a `.glb`) are left as they are.
- libsk: for a texture with the extension, the dependency list names the variant this
  GPU can use instead of the texture's own image (only that file downloads), the
  worker reads it instead of decoding an image, and the texture is uploaded as it is.
  Without a usable variant (or if the file can't be read) the texture's own image is
  used as before. A missing variant is a dependency with a fallback: the texture's
  own image is fetched instead (with a warning), so the web gets it too.
- Checked by eye: FlightHelmet with its PNGs and compressed, side by side, match
  (normal maps included).

Loading Sponza and FlightHelmet (`make loadbench DESKTOP=1 [KTX=1]`), desktop GL
(NVIDIA), files local:

|                                   | PNG / JPEG textures | compressed   |
|-----------------------------------|---------------------|--------------|
| in the background: total          | 0.85-0.92 s         | 0.13 s       |
| in the background: worst frame    | 31-35 ms            | 21-22 ms     |
| synchronously                     | 1.35-1.37 s         | 0.11-0.12 s  |

FlightHelmet alone on the Pixel 9 (WebGL2, ASTC), files in its cache:

|                                   | PNG textures        | compressed   |
|-----------------------------------|---------------------|--------------|
| in the background: total          | 1.98-2.03 s         | 0.45 s       |
| in the background: worst frame    | 73-113 ms           | 97-102 ms    |
| synchronously                     | 1.36-1.41 s         | 0.09-0.10 s  |

The phone's worst frame while loading in the background stays about 100 ms either way:
the first frame drawing the loaded model compiles its shaders (~220 ms seen before on
WebGL2), and uploads of several large textures can land in one frame (TASKS).

## Not in this plan

- Smaller ASTC blocks (6x6, 8x8: 3.6 and 2 bits a pixel) for smaller downloads, at
  some quality.
- Models in a `.glb` (images inside the file): the tool would have to take the images
  out; KHR_texture_basisu.
- KTX 2 and its zstd supercompression; Basis transcoding as a second, optional module
  if single-file assets turn out to matter.
- sRGB formats (with sRGB-correct filtering), when the renderer moves to linear
  sampling.
