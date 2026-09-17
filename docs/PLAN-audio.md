# Plan: Audio — streamed music and mixing off the main thread

Status: **implemented (2026-09-16).** Decisions 1–3 accepted as recommended; see
"As built".

Restores two things librl had (through raylib's audio) that libsk lost. The
general "decode resources in the background" work is a separate roadmap item
(docs/ROADMAP.md, loading pipeline).

## Where we are

| | librl (raylib / miniaudio) | libsk today |
|---|---|---|
| Music | streamed: decoded while playing | decoded fully at `sk_audio_create` |
| Mixing | on the audio device's thread | on the main thread (`sk_audio_tick` pushes each frame and while pacing) |

Measured: `sk_audio_create` on the 6 MB example MP3 takes **215 ms** and holds
**~108 MB** of float PCM (13.5 million stereo frames). And because mixing is on the main thread, any slow frame
(for any reason) leaves the audio device without samples: an audible gap.

## Proposed design

### 1. Stream long audio

- An Audio whose file is larger than **1 MB** (roughly a minute of MP3) is
  streamed: it keeps the encoded file in memory and decodes while playing. Smaller
  files decode fully at create, as now, so sound effects start instantly.
- Each playing Sound of a streamed Audio has its own decoder (dr_mp3, dr_wav and
  stb_vorbis all decode incrementally from memory), so one Audio can play on
  several Sounds at once.
- `sk_audio_create(path)` and the Sound API don't change. Play (rewind), pause,
  resume, stop, loop, volume and pitch behave the same for both kinds.
- Expected: create becomes a file read (a few ms); memory drops ~10x; decoding
  costs a little CPU during playback, on the mixing thread.

### 2. Mix on the audio device's thread

- Switch sokol_audio to callback mode: the device asks for samples and the mixer
  fills them, on the audio thread on desktop. A slow frame no longer causes a gap.
- The game thread and the mixer share Sound state (playing, position, volume,
  pitch, loop, audio) and Audio resources. A mutex protects them: the mixer holds
  it while filling a buffer (short, a few ms at most); API calls hold it while
  changing a Sound or releasing an Audio, so PCM is never freed while being mixed.
- Web: sokol_audio also runs the callback there (from the browser's audio
  callback on the main thread), so the same code works; the mutex is a no-op
  without threads.
- Frame pacing no longer needs to feed audio while sleeping (`sk.c` pacing loop
  and the per-frame `sk_audio_tick` go away).
- Headless builds keep no audio device; a test hook pulls mixed samples directly.

## Decisions

1. **Stream automatically above 1 MB**, versus an explicit choice (a flag, or a
   separate noun). Recommend: automatic. It matches the file to the behavior
   without the game deciding; revisit if a game needs to force either mode.
2. **Mix on the device thread with a mutex**, versus a lock-free command queue.
   Recommend: mutex. Critical sections are tiny and rare on the game side; a queue
   adds complexity that measurements don't justify yet.
3. **Callback mode on web too** (one code path), versus keeping the push model
   there. Recommend: callback mode everywhere.

## As built

- `sk_audio_create` on the example music: **215 ms → 5 ms**; memory **~108 MB → 6 MB**
  (the MP3 file). Streamed Audio knows its length at create (dr_mp3 counts frames
  from headers in ~1 ms), so looping works exactly like decoded Audio.
- Streamed and decoded Audio produce identical samples (unit test, block by block,
  WAV/OGG/MP3 with loop, pitch and resampling), including rewinds.
- One recursive mutex (`sk_audio_lock`) guards Sounds and Audio; decoding a file at
  create happens outside it. Seeking a streamed MP3 far forward takes up to ~80 ms
  on the mixer thread, which only happens when a sound's position jumps (rewinding
  to the start is cheap).
- `make test SANITIZE=thread` (also `address`, `undefined`) builds the library into
  the tests with a sanitizer; ThreadSanitizer runs with ASLR off (`setarch -R`),
  which newer kernels need. A concurrency test mixes on a second thread while the
  game thread plays, stops, retargets, creates and destroys; TSan is clean, and was
  checked to flag an unlocked setter.
- Frame pacing sleeps in one go instead of slices (nothing to feed).
- `examples/audio.c`: press S for a 300 ms stall; music should keep playing.

## Verification

- Unit tests (headless, pulling samples from the mixer):
  - streamed decode matches full decode sample for sample for MP3, OGG and WAV,
    including across buffer boundaries and at loop points
  - play, pause, resume, stop, loop and pitch give the same output for streamed
    and fully decoded Audio
  - two Sounds playing one streamed Audio at different positions
  - releasing an Audio while its Sound is playing is safe
- Timing and memory before/after for `sk_audio_create` on the example music.
- `examples/audio.c`: music keeps playing through a deliberately slow frame
  (e.g. a key that sleeps 300 ms), checked by ear on desktop and web.
- A `SANITIZE=thread` (TSan) build of the audio tests; `make test`, `make smoke`,
  `make check`, `make webcheck`; CI.
