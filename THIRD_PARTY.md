# Third-party components

The firmware uses board support from [Felucca](https://github.com/hugelton/Felucca) (`GPL-3.0-only`) at build time: FM-1 HAL headers, startup/vector code, linker script, USB-MIDI/update support, loader, and package maker. NIGHTFALL's synthesis, sequencing, effects, application UI, and project format are separately implemented. See `firmware/NOTICE.md` for the precise boundary. The JieLi SDK SPL/config assets used by the experimental packager carry Apache-2.0 licensing in the upstream SDK.

The web updater and editor include Felucca Web App-derived code (`GPL-3.0-only`). Its license text and attribution are in `web/source/`. Inter Tight uses the SIL Open Font License; Fukiai icons use MIT; the FM6 algorithm tables derived from msfa use Apache-2.0. Their license files are kept in `web/source/LICENSES/` and `web/editor/fonts/`.

The optional `PIANO_HD` sample is derived from Versilian Studios' VCSL Kawai grand piano recordings under CC0 1.0. Details and source links are in `web/editor/samples/PIANO_HD.txt`.

