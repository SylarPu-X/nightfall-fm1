# NIGHTFALL FM-1

NIGHTFALL is an independent, cyberpunk-inspired performance firmware and browser studio for the M-VAVE FM-1. This repository contains the synthesizer, sequencer, FX, display renderer, USB-MIDI control protocol, web simulator, device editor, and browser updater source.

**Experimental source release — not a qualified firmware release.** One FM-1_999 unit has booted the current experimental build and passed USB identity, parameter round-trip, MIDI note, and spectral-terrain protocol tests. Full panel/audio regression, long-run stability, official-firmware rollback, and recovery without external hardware are not proven. The test USB VID/PID and product identity must be replaced before general distribution. For these reasons, this public repository intentionally contains **no flashable `.fwsc` package**. Do not flash the linker image in `firmware/build/`.

## Layout

- `firmware/` — original NIGHTFALL application code, target adapter, tests, build and experimental packaging tools.
- `web/` — static performance simulator, device editor, USB-MIDI bridge, and browser updater source. The public copy has no built-in firmware package; it accepts a user-selected file after device and risk checks.

## Try the source locally

```sh
cd firmware
make test
make audio-test
make preview
```

To preview the browser studio, serve `web/` as the document root (for example, `python3 -m http.server 8000 --directory web`) and open `http://127.0.0.1:8000/`. The simulator runs without a device. USB-MIDI access requires a compatible browser and a connected FM-1.

The native target build requires a JieLi toolchain, the Felucca board-support checkout, and the vendor SPL/config assets. `firmware/README.md` and `firmware/NOTICE.md` document the build, current limits, and licensing. The build was tested against Felucca commit `a53ecf428c29c780108fd70a31b71a32a6b5c313` and JieLi SDK commit `d179b4484759423312073f5fbb232501aa491047`. These are external dependencies, not vendored application code.

## License and attribution

NIGHTFALL source is released under **GPL-3.0-only**; see `LICENSE`. It uses Felucca's GPL-3.0-only FM-1 startup, HAL, USB-MIDI/update infrastructure and packaging tools as external dependencies. The web editor/update code also contains GPL-licensed Felucca Web App components. Fonts, the optional piano sample, and other third-party pieces retain their own notices; see `THIRD_PARTY.md`, `web/source/LICENSING.md`, and `web/editor/samples/PIANO_HD.txt`.

This is an independent community project, not an M-VAVE product or an official update. Film-inspired visual direction does not include copyrighted film footage, audio, or logos.

