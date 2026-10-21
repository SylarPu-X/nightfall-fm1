# Third-party board support

The native FM-1 target build depends on unmodified files from Felucca
(Hügelton Instruments, copyright 2026 Leo Kuroshita), licensed
GPL-3.0-only: `firmware/hal/*.h`, `firmware/crt0.S`,
`firmware/hal/fm1_vec.S`, `firmware/hal/fm1_isr.S`, and
`firmware/app.ld`. Those files are supplied by an external checkout at build
time and are not copied into this source tree. The resulting combined binary
must be distributed under compatible GPL terms with the dependency's source
and notices.

The rest of this tree is NIGHTFALL application code, independently implemented
for the FM-1. The default vendor-bulk build does not include or modify
Felucca's audio engines, sequencer, effects, UI, project format, USB stack or
updater. The separate recovery-MIDI first-board-test variant reuses the GPL
USB-MIDI and M-UPGRADE support as external board/update infrastructure. Its
build makes a source derivative with only two USB display strings changed;
the upstream checkout is not edited. No Felucca synth, UI or sequencer is used.

The *experimental, non-release* packaging script separately invokes
Felucca's GPL-3.0-only package maker and embeds its RAM update loader. This is
only update infrastructure: the application image remains NIGHTFALL's own
implementation. The owner-only website source archive includes this project,
the matching Felucca firmware/packer source and licenses, and JieLi SPL/config
assets with their Apache-2.0 license. A wider release still needs a complete
corresponding-source audit and a stable device identity.
