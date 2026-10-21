# FM-1 panel interaction map (development source `FM-1_999`)

The installed `FM-1_999` image includes the behavior below. This
map describes the independent NIGHTFALL source and firmware build;
hardware knobs, page timing, LEDs and rollback still require a controlled
physical test before release.

| Control | Default / HOME | Context-specific behavior |
| --- | --- | --- |
| MASTER potentiometer | Post-FX stereo output volume, from mute to full | Global; city and scope intentionally remain pre-MASTER |
| PLAY | Start/stop arrangement from bar 1 | Global |
| REC | Toggle live-key step recording | From GLO/SEQ/SAVE, entering REC returns to HOME so keys can record |
| OCT− / OCT+ | Shift physical keyboard in octaves, −2 to +2 | On SEQ, change the 16-step bank and playing pattern length |
| SELECT encoder | Tempo 40–240 BPM | FX: module; SEQ: focus step; SAVE: slot |
| ALGORITHM encoder | Select DRUM/BASS/CHORD/LEAD track | Global; selected track is always labelled on-screen |
| PRESETS encoder | MIDNIGHT / NEON RUN / AFTER RAIN | Only while stopped and not recording; blocked turns show a reason |
| K1–K4 | BRIGHT / DENSITY / SPACE / MOTION performance macros | See page table |
| HOME button | Enter HOME; press again to switch city/scope | Active page button returns to HOME on a second press |
| FX, ENV, LFO, EDIT, GLO, SEQ, ARP, SAVE buttons | Enter named page | Same button again returns to HOME |
| SEL button | Enter SCALE page; press again to cycle modes | FX: bypass; ARP: on/off; SEQ: focus-step on/off; SAVE: action |

| Page | K1 | K2 | K3 | K4 | Piano keys |
| --- | --- | --- | --- | --- | --- |
| HOME | Brightness | Preset lead density | Delay/reverb space | LFO motion | Perform; optional scale lock/chord/arp |
| FX | First selected-FX parameter | Second | Third | Mix | Perform |
| ENV | Attack | Decay | Sustain | Release | Perform |
| LFO | BPM-synced rate | Depth | Tone/amp/pitch target | Sine/triangle/square/saw | Perform |
| EDIT | Wave | Spread | Oscillator octave | Voice level | Perform |
| GLO | Drum level | Bass level | Chord level | Lead level | White keys 1–4 mute, 5–6 scene, 7 fill, 8 auto-arrange; no notes sound |
| SEQ | Focus-step pitch | Velocity | Length | Probability | White keys 1–16 toggle steps in the current bank; no notes sound |
| ARP | Order | Division | Gate | Octave range | Perform; LEAD track is arpeggiated when enabled |
| SAVE | SAVE/LOAD mode | Unassigned | Unassigned | Unassigned | Perform (but release notes and stop playback before action) |
| SCALE | Unassigned | Unassigned | Unassigned | Unassigned | Perform with current scale mode |

SAVE slots are volatile RAM only. The UI says so. A second SEL press within
four seconds is required before overwriting an occupied slot. SAVE and LOAD are
blocked during playback, recording or while a physical key remains pressed.
Use the webpage's `.nfpr` project export before powering off; RAM slots are
**not** a substitute for persistent storage.

In SEQ, SELECT focuses a step, K1–K4 change its pitch, velocity, length and
probability, and the 16 white keys toggle its note. Velocity zero is OFF; a
positive K2 turn can enable an empty step. On GLO and SEQ, edit-command keys
never sound. On other pages the keyboard remains playable. SEQ exposes four
16-step banks; OCT−/OCT+ selects a bank and sets the loop length to 16/32/48/64.
This bank interaction has compiled and emulator coverage but still needs
per-button verification on physical hardware.
