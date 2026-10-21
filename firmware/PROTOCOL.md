# NIGHTFALL device packet protocol (version 1)

The recovery-MIDI build carries application packets in non-commercial
SysEx: `F0 7D 4E 46 01 <8-to-7 packed packet bytes> F7`. It preserves the
separate Felucca-compatible `00 59` updater route. This transport is
validated on one physical FM-1 for HELLO/DETAIL and reversible controls. The alternate
development-only vendor-bulk build carries the same packet bytes unchanged.
All integer
fields here are unsigned and little-endian unless stated otherwise.

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 2 | Magic `46 4e` (`FN`) |
| 2 | 1 | Version `01` |
| 3 | 1 | Command |
| 4 | 1 | Sequence, echoed by the reply |
| 5 | 2 | Payload length, at most 240 |
| 7 | N | Payload |
| 7+N | 2 | CRC-16/CCITT-FALSE over all previous bytes |

Each request has one reply with command bit 7 set. The first reply payload
byte is status: `0` success, `1` invalid request/state, `2` unsupported
command, `3` project validation failure. A malformed frame has no reply. A
transport may split a frame at any byte; `nf_link_feed` reassembles it.

Commands: `01` HELLO, `02` STATUS, `03` NOTE_ON, `04` NOTE_OFF, `05` TEMPO,
`06` FX, `07` STEP, `08` PLAY, `09` PROJECT_SAVE, `0a` PROJECT_LOAD,
`0b` PROJECT_EXPORT, `0c` PROJECT_IMPORT_BEGIN, `0d` PROJECT_IMPORT_CHUNK,
`0e` PROJECT_IMPORT_COMMIT, `0f` PROJECT_IMPORT_ABORT, `10` CONTROL,
`11` DETAIL, `12` LIVE. The C definitions in
`src/nightfall.h` are authoritative for field ranges.

`CONTROL` has four bytes `[group, index, parameter, value]`. All invalid
indices, ranges and playing-state conflicts are rejected without changing the
target. Groups are: `0` page; `1` selected track; `2` preset; `3` performance
macro; `4` track level (`parameter=0`) or mute (`1`); `5` track ADSR; `6` LFO;
`7` voice edit; `8` arpeggiator; `9` queued A/B scene (`parameter=0`), auto
arrange (`1`) or fill queue (`2`); `10` scale mode; `11` record arm; `12` HOME
view; `13` selected FX; `14` selected project; `15` save/load operation;
`16` pattern length (16, 32, 48, or 64); `17` browser-monitor mute
(`value=1` renews a 2.5-second session lease, `0` releases it). The mute
is applied after pre-MASTER analysis and expires automatically if USB or the
browser disappears; older firmware rejects this group. For
singleton groups, index and parameter are zero. Track groups use the track as
index; four-value groups use the control index, except ADSR/voice edit, where
track is index and the control is parameter. `DETAIL` has an empty request and
a fixed 117-byte response (including status): tempo LE16, playing, recording,
page, selected track, preset, scene, queued scene, auto, scale, arp, HOME view,
bar, step, four macros, four levels, four mute flags, six FX enables, 24 FX
values, 16 ADSR values, four LFO values, 16 voice values, four arp values,
eight selected-track step bitmasks, four edited flags, project-valid mask,
selected FX, selected project, save/load operation, fill queue, pattern length,
focused step. The response
is a snapshot, not a command to flash firmware.

`LIVE` is a read-only, variable-length snapshot of the actual audio/visual
state. The first 54 response bytes are status (0), device clock LE32 (1–4),
page (5), selected track (6), playing (7), recording (8), step (9), bar (10),
MASTER position 0–100 (11), audio-late counter LE16 (12–13), active voice count
(14), tempo LE16 (15–16), scene (17), queued scene (18), HOME view (19),
selected FX (20), 32 pre-master scope values (21–52), and event count 0–20
(53). Each event
uses eight bytes: serial low byte, note, track, source (`KEY=0`, `MIDI=1`,
`SEQ=2`, `WEB=3`), velocity, envelope 0–255, onset age in 10 ms units, and
end age in 10 ms units (`255` means live). The scene renderer and browser
mirror use the same source/pitch/age/envelope semantics. Events are newest
first and capped to keep a whole snapshot inside the 240-byte packet limit.
The terrain-capable variant appends a one-byte terrain generation counter and
20 log-spaced, pre-MASTER spectral band levels (0–255) after the event list.
Its maximum response is 235 bytes. Clients must also accept older LIVE replies
without the 21-byte terrain suffix; in that case the web cannot claim exact
sound-terrain synchronization.

Project export request payload is `[slot, chunk]`; a successful reply is
`[0, chunk, 180 project bytes]`. Chunks 0–6 cover the 1260-byte version 2
project file. Import starts with `[slot]`, then seven `[slot, chunk, 180 bytes]`
requests in any order, and ends with `[slot]` COMMIT. A CHUNK reply includes
the seven-bit received mask after status. COMMIT checks every chunk and the
project CRC and fields before changing the selected RAM slot. ABORT `[slot]`
discards the pending transfer. Only one import may be active. An export while
an import is active is rejected because they share a staging buffer.

No firmware update, arbitrary memory access or flash-write command exists in
this application protocol. The website's experimental update flow uses the
separate legacy-compatible updater and must keep its explicit unqualified
firmware warning until rollback and recovery are proven.
