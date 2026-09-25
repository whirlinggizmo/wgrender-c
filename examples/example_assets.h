#ifndef EXAMPLE_ASSETS_H
#define EXAMPLE_ASSETS_H

/* Where the examples load assets from. Asset paths in the examples are LOGICAL
 * (e.g. "sprites/logo/wg-logo.png") and resolve against this base:
 *   - desktop: a local directory, relative to the run cwd (the project root).
 *   - web:     "assets", relative to the page, fetched on a cache miss then stored
 *              in idbfs. Relative and not "/assets" so the site works wherever it is
 *              hosted: at a domain root (tools/serve.py, which mounts examples/assets
 *              at /assets) and equally under a path, as GitHub Pages serves a project
 *              at /<repo>/. Benchmarks under bench/ keep the absolute form; they are a
 *              local tool and are not published.
 * Pass it to wgr_asset_set_host() in on_init before ensuring any asset. */
#ifdef __EMSCRIPTEN__
#  define EXAMPLE_ASSET_BASE "assets"
#else
#  define EXAMPLE_ASSET_BASE "examples/assets"
#endif

/* The animated character the examples load: skinned, with its blob shadow in material
 * slot 0 and its body in slot 1, and clips in the order idle, run, tpose, walk (the
 * examples pick them by number). Swap it here. */
#define CHARACTER_PATH "models/woman_casual/woman_casual.glb"

#endif // EXAMPLE_ASSETS_H
