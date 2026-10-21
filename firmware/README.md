# NIGHTFALL FM-1

Independent firmware project for the M-VAVE FM-1. This is **not** a Felucca
fork: NIGHTFALL owns its application, synthesis, sequencer, communication
packet and renderer. It runs as a host-compiled vertical slice and
cross-compiles as an FM-1 application image. The recovery-MIDI variant was
installed on one FM-1 on 2026-10-15 as `FM-1_999`. It booted, enumerated as
`NIGHTFALL FM-1`, and its screen, piano-key city reaction, sound and MASTER
volume were confirmed by the owner. An original vendor-bulk USB device layer
is linked separately for bench testing, but has not enumerated on hardware.
The native build uses the
unmodified GPLv3 FM-1 board-support headers, startup and linker script from
Felucca as external dependencies; see `NOTICE.md`. That dependency is hardware
support, not the Felucca sound engine, UI, sequencer or application.

There is **no qualified stable-release NIGHTFALL package** here yet. The native
`nightfall-app.bin` is a linker image, not an installer; do not flash it.
`NF_USB_MODE=midi python3 tools/build_native.py` cross-builds a separate
first-board-test image, and `tools/package_experimental.py` creates a
**experimental** package used for user-authorized hardware tests and now
distributed with explicit risk warnings; it is not a stable release. That variant
reuses the external GPL USB-MIDI/update infrastructure, plus the GPL packer
and RAM loader and Apache-2.0 JieLi SDK files; the NIGHTFALL app, UI, synth,
sequencer, FX and project format remain independent. It answers the standard
update identity `FM-1_999`, accepts an M-UPGRADE session, and clears a staged
loader on boot. NIGHTFALL's own checked packet protocol is carried over a
distinct non-commercial USB-MIDI SysEx envelope, without changing the updater.
The package parses correctly in an independent browser-side package
reader, and external update-entry/RAM-loader host simulations pass with it,
including interrupted/corrupt transfers. The owner-authorized `FM-1_997` to
`FM-1_999` update, independent identity query, NIGHTFALL HELLO/DETAIL, and
reversible tempo/FX/pattern-length readback all pass on hardware. A note
on/off command was accepted; the owner confirmed local sound, city response
and MASTER control. Rollback from NIGHTFALL, complete panel/FX/sequence
coverage and long-run stability are still unverified. The USB VID/PID remains
test-only and is not suitable for a qualified stable release.

The first installed `FM-1_997` image omitted the physical PB6/ADC4 MASTER
potentiometer from the audio path. The current source reads and smooths that
ADC input and scales the final stereo mix. Instruction-level target simulation
confirms mute, midpoint, full level and live 0-to-full/full-to-0 turns. This
`FM-1_998` was an unflashed MASTER-only candidate. The installed `FM-1_999`
source also includes independent per-key note release, working HOME density and
space macros, per-page parameter feedback, a four-knob step editor, explicit
SAVE/LOAD confirmation, and an updated city/LFO/ARP renderer. These fixes now
run on the owner's hardware; complete physical button and
audio-regression testing remains to be done.

## Implemented prototype

- Four tracks, eight shared voices, physical/MIDI/sequence note sources.
- Three ready-to-play progressions (MIDNIGHT, NEON RUN, AFTER RAIN), A/B scene
  selection, a 16-bar automatic arrangement, queued drum fill, and a 64-step
  pattern per track with step/live-note recording.
- Twelve original themed sound patches, four per song preset; see
  `SOUND_LIBRARY.md`. No copyrighted film audio or third-party sample is bundled.
- Four bounded sound types and a six-stage FX chain: FILTER, DRIVE, CHORUS,
  PHASER, DELAY and REVERB. The physical FX page edits their controls.
- The panel pages HOME, FX, SAVE, ENV, LFO, EDIT, GLO, SEQ, ARP and SCALE, with
  mapped page keys and seven encoders. HOME has an event-driven 240 × 240
  RGB565 city scene. The current development source replaces the original
  audio-peak plot with a 20-band, 20-row pre-MASTER spectral terrain, analysed
  outside the audio interrupt and sent to the web in the optional LIVE suffix.
  The first terrain build was flashed to one FM-1 and reported working but
  visibly choppy, with a frozen last waveform on silence. The current source
  advances zero-valued terrain rows on silence, fades history, interpolates
  the display, and uses a saturated green RGB565 palette. This revision was
  flashed to one FM-1_999 and passed USB identity and LIVE protocol tests;
  the owner's visual judgement of the LCD palette is pending.
  City roof height is
  pitch, interior level is envelope, onset is the only cause of a moving scan,
  and released events recede for three seconds. Geometry is fixed-point and
  allocation-free. The physical keyboard supports scale snap and simple
  one-key minor triads and a BPM-synced lead-key arpeggiator. ENV/EDIT controls
  feed the synth, and the LFO feeds live pitch, amplitude or tone modulation.
- A compact packet format with version and CRC checks,
  plus validated commands for notes, steps, transport, FX, the full panel
  control set and a compact state snapshot. A byte
  stream parser handles split USB-style reads; seven checked chunks transfer a
  whole project without mutating a slot until commit. The installed variant
  transports packets over USB-MIDI SysEx; a target USB bulk adapter remains a
  bench-only alternative. See `PROTOCOL.md`.
- Three project snapshots in RAM with checksums, now including performance
  controls as well as patterns and FX. They are not yet saved to
  nonvolatile memory and disappear on power-off.
- An original A/B NOR journal with read-back and commit marker, tested against
  interrupted writes in a simulated flash device. It is deliberately **not**
  connected to the FM-1 flash controller until the data-sector ownership and
  audio-safe erase path are verified on the target.
- Native tests and a rendered PPM preview. The physical editor now uses SELECT
  to focus a sequencer step, white keys to toggle 16 steps in each of four
  banks, and K1–K4 to
  edit pitch, velocity, length and probability; holding REC from SEQ/GLO/SAVE
  routes to HOME so keyboard recording is possible. In SAVE, SEL confirms an
  overwrite within four seconds, and the screen states that slots are RAM-only.

Run `make test`, `make preview` and `make audio-test` on a host with a C11
compiler. The visual previews
are `build/city.ppm` and `build/fx.ppm`. The native cross-build is `python3 tools/build_native.py`
inside the project build container, with `JIELI_TOOLCHAIN` pointing to the
toolchain and `FM1_BOARD_SUPPORT` to an unmodified FM-1 BSP checkout. Its output
is `build/native/nightfall-app.bin`; it is deliberately **not** packaged.

## Hardware completion gates

1. Physical boot, USB-MIDI identity, LCD, piano-key city response, MASTER and
   audible output passed on one FM-1. Dedicated NIGHTFALL parameter exchange
   passed, including reversible write/read/restore. Full knob/page/FX/sequencer coverage,
   CPU/audio performance and long-run stability remain to be measured.
2. The recovery-MIDI variant's device-side NIGHTFALL-to-NIGHTFALL update passed.
   A successful return to official V15 is still required before treating rollback
   as proven. Then validate
   the separate vendor-bulk adapter's
   data integrity and audio scheduling. Replace the private bench-only USB
   TEST VID/PID with an allocated product identity. Bind the journal to a
   verified NOR region, and finish sample playback, complete sound/FX engines,
   browser control QA and the full interaction map. These are **not done**.
3. Build a package with an allocated NIGHTFALL product/version identity and
   verify the full boot/update/rollback path on a recoverable device.
4. The public experimental `FM-1_999` package and risk-confirmed browser updater
   are for informed testers only. Do not treat them as a stable release until the
   remaining gates pass. A browser simulator alone cannot certify an image.

The existing webpage is the interaction/design reference. This repository is
the source of truth for the new device firmware, not a reskinned third-party
binary.

## Current flashed experiment

The source after the first `FM-1_999` terrain build fixes silence-frozen
terrain history, reduces LCD transfer load with partial-row updates, and
interpolates the wireframe between 70 ms audio slices. It changes the actual
device RGB565 line palette from near-white/amber through pale green to
saturated `#48ff66` (RGB565 `0x4fec`). A new
`CONTROL` group 17 offers a temporary, automatically expiring local-output
mute for browser-only sound; the browser has its own independent volume.
Host tests, audio renders, native cross-compilation, image slot/RAM checks,
and package CRC/decryption round-trips pass. The `FM-1_999` candidate was
flashed to one device, restarted, and passed USB identity, parameter
write/read/restore, note on/off, and 20-band terrain protocol checks. Silent
terrain generations continued advancing with zero-valued rows and zero new
audio underruns over a three-second read-only measurement. Human inspection
of LCD color, sound quality, and panel controls is still required;
rollback/recovery remain unproven.
