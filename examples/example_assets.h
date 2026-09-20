#ifndef EXAMPLE_ASSETS_H
#define EXAMPLE_ASSETS_H

/* Where the examples load assets from. Asset paths in the examples are LOGICAL
 * (e.g. "sprites/logo/wg-logo.png") and resolve against this base:
 *   - desktop: a local directory, relative to the run cwd (the project root).
 *   - web:     the served origin (tools/serve.py mounts examples/assets at
 *              /assets), fetched on a cache miss then stored in idbfs.
 * Pass it to wgr_asset_set_host() in on_init before ensuring any asset. */
#ifdef __EMSCRIPTEN__
#  define EXAMPLE_ASSET_BASE "/assets"
#else
#  define EXAMPLE_ASSET_BASE "examples/assets"
#endif

#endif // EXAMPLE_ASSETS_H
