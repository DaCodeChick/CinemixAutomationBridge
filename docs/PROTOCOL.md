# D&R Cinemix MIDI Protocol — Recovered Knowledge

This document records everything known about the MIDI automation protocol of the
D&R Cinemix console, as recovered from the legacy
[CinemixAutomationBridge](https://github.com/ZioGuido/CinemixAutomationBridge)
by Guido Scognamiglio (GSi), and from the accompanying project page at
[genuinesoundware.com](https://www.genuinesoundware.com/?a=page&p=TheCinemixProject).

**Provenance.** Every fact below is labeled:

* **[code]** — stated or implemented in the legacy source code (Plugin.h/Plugin.cpp).
* **[notes]** — stated in the legacy `MIDI_Cinemix.txt` protocol notes.
* **[readme]** — stated in the legacy README / GSi project page.
* **[inferred]** — deduced by us from the above, not independently verified.
* **[uncertain]** — plausible but unverified; see the discussion.

Nothing in this document has been re-verified against physical hardware yet.
Treat this as a map drawn by someone who already walked the territory, not as a
fresh survey.

---

## 1. Physical MIDI topology

The Cinemix's automation MIDI is not exposed on DIN5 sockets on the console.
The console hosts an "ASYNC INTERFACE" on a **DB25** connector, carrying **two
independent MIDI input/output pairs** (MIDI+, MIDI−, ground per direction).
The original PowerVCA automation computer used a Music Quest MQX-32M ISA card
with the same pinout on a DB9. **[readme]**

* **Pair 1 (port 1)** drives the **LO part** of the console: the first 24
  channel strips.
* **Pair 2 (port 2)** drives the **HI part**: strips 25 and above **plus the
  master section**. **[readme]**

The console reports its own split: press `SETUP` then the left-arrow key. The
legacy author's console reads `LO=24 HI=12 MSTR=hi`. **[readme]**

MIDI channels are assigned per side (see below), so the two ports each carry a
distinct channel set. The legacy plugin broadcasts most management messages to
both ports; side-specific position data is routed to one port. The hardware
ignores channels that do not belong to its side (verified implicitly by ~5
years of legacy use with broadcast sends). **[code, inferred]**

### Cable wiring (DB25/DB9 → 4× DIN5 male)

**[readme]** — from the schematics book, page 34 (AS Con./DigPower):

| DB25/DB9 pin | Signal       | DIN pin |
| ------------ | ------------ | ------- |
| 1            | MIDI+ OUT 1  | 4       |
| 14           | MIDI− OUT 1  | 5       |
| 2            | MIDI+ OUT 2  | 4       |
| 15           | MIDI− OUT 2  | 5       |
| 3            | MIDI ground  | 3 (all) |
| 16           | MIDI− IN 2   | 5       |
| 4            | MIDI+ IN 2   | 4       |
| 17           | MIDI− IN 1   | 5       |
| 5            | MIDI+ IN 1   | 4       |

The MIDI interface on the computer side needs **two MIDI inputs and two MIDI
outputs**.

---

## 2. Remote-control mode

The console is put into **remote control MODE** by sending to **both outputs**:

```
CC #127, value 127, MIDI channel 5
```

In this mode the console disconnects its physical controls (faders, mutes,
joysticks) from the internal analog control paths and drives them from MIDI.
Joystick movement no longer moves the X/Y LEDs — a quick cable test. **[readme]**

Release remote mode by sending a single **system-reset byte `0xFF`** to both
outputs, or by power-cycling the console. The legacy plugin additionally sends
`CC #127 = 0, ch 5` before the `0xFF` when deactivating. **[readme, code]**

Observed PowerVCA startup traffic (not required by the bridge, recorded for
reference): `CC#127=127 ch5`, `CC#65=1 ch5`, reset of all SEL/touch faders,
then `CC#127=127 ch5`, `CC#65=15 ch5`. On exit PowerVCA sends `0xFF`. **[notes]**

---

## 3. Channel-strip addressing

For a console split into **LO** (strips 1..L) and **HI** (strips L+1..S):

* LO fader positions: MIDI channel **1**; LO mute/touch/SEL: channel **3**.
* HI fader positions: MIDI channel **2**; HI mute/touch/SEL: channel **4**.
* Master section fader: channel **5**; joystick axes: channel **2**; joystick
  mutes/SEL: channel **4**; AUX mutes: channel **5**.

With `w` = strip index within its side (0-based):

| Control                | CC number                          | Channel | Value   |
| ---------------------- | ---------------------------------- | ------- | ------- |
| Strip fader, Chan path | `4w` (pair `4w`, `4w+1`)           | 1 / 2   | 0..127  |
| Strip fader, Mix path  | `4w+2` (pair `4w+2`, `4w+3`)       | 1 / 2   | 0..127  |
| Strip mute, Chan path  | `2w`                               | 3 / 4   | 2=OFF, 3=ON |
| Strip mute, Mix path   | `2w+1`                             | 3 / 4   | 2=OFF, 3=ON |
| Strip touch/SEL, Chan  | `64 + 2w`                          | 3 / 4   | 5=release, 6=touch; 1=SEL press |
| Strip touch/SEL, Mix   | `64 + 2w + 1`                      | 3 / 4   | 5=release, 6=touch; 1=SEL press |

**[notes, code]** — the notes describe the first 24 strips on channels 1/3 and
say "channels 24..32: same thing with channel += 1" for the HI side. The legacy
code implements exactly the scheme above for its 24 LO + 12 HI strips, with
numbering restarting at CC 0 on the HI side.

The legacy author's console: **32 mono inputs + 4 stereo inputs** on 36 strips.
Strips 25–28 are the stereo pairs (S1..S4); all other strips are mono. Every
strip is dual-path: a CHANNEL path ("upper", 60 mm motorfaders) and a MIX path
("lower", 100 mm motorfaders). **[readme]**

### The two-CC fader pairs **[uncertain]**

Each fader occupies *two* consecutive CC numbers (`4w`+`4w+1` for Chan,
`4w+2`+`4w+3` for Mix; the master fader uses `0`+`1` on ch 5). The notes say
"Val: 0 ~ 127 (Interpolated)". The legacy plugin:

* sends **only the even CC** of each pair (7-bit target position); **[code]**
* accepts **either CC** of the pair as a 7-bit position on input. **[code]**

Possibilities we could not decide from the sources:

1. The odd CC is a fine (low 7 bits) of a 14-bit position — PowerVCA may use it
   for precision. If so, the legacy input handling (either CC as a full 7-bit
   position) is a tolerable simplification because the console apparently
   mostly reports via the even CC.
2. The console sends interpolated motor positions during travel on one CC and a
   settled value on the other.
3. The pair simply duplicates the value.

The new bridge defaults to the **proven legacy behavior** (send even CC only;
accept either CC as 7-bit). A profile flag `faderResolution` (`sevenBit` /
`fourteenBit`) exists for consoles where the 14-bit interpretation proves
correct; it is **unverified**.

---

## 4. Master section

All master-section traffic goes to **port 2** (HI side). **[code, notes]**

| Control        | CC     | Channel | Value            |
| -------------- | ------ | ------- | ---------------- |
| Master fader   | 0 (pair 0/1) | 5 | 0..127 (not motorized, no touch sensor) |
| Master SEL     | 64     | 5       | 1 = pressed (console→plugin); plugin sends mode 0..3 |
| Joy 1 X        | 48     | 2       | 0=left .. 127=right |
| Joy 1 Y        | 50     | 2       | 0=top .. 127=bottom |
| Joy 1 mute     | 24     | 4       | 2=OFF, 3=ON |
| Joy 1 SEL      | 88     | 4       | plugin→console, mode 0..3 |
| Joy 2 X        | 52     | 2       | 0=left .. 127=right |
| Joy 2 Y        | 54     | 2       | 0=top .. 127=bottom |
| Joy 2 mute     | 26     | 4       | 2=OFF, 3=ON |
| Joy 2 SEL      | 90     | 4       | plugin→console, mode 0..3 |
| AUX n mute     | 96     | 5       | 2n = OFF, 2n+1 = ON (n = 1..10) |

Notes:

* Joystick SEL addresses (CC 88/90, ch 4) overlap the range the legacy plugin's
  blanket "set all strip modes" sweep touches on the HI side; the legacy code
  wrote to CC 64..111 on ch 4 indiscriminately. The new bridge scopes mode
  sweeps to the strips that exist in the profile, then sets joystick SEL
  explicitly (see Compatibility notes).
* The master fader is not motorized and has no touch sensor; the master SEL
  button is therefore used to set the mode of **all** faders at once.
  **[readme]**
* AUX mutes are a single CC carrying the ten mutes as value pairs 2/3 … 20/21.
  **[code, notes]**

---

## 5. SEL / touch state machine

SEL values (sent to a strip's touch/SEL CC, or ch5 CC64 for the master):

| Value | Mode      | Notes                  |
| ----- | --------- | ---------------------- |
| 0     | Isolated  | control isolated       |
| 1     | Read      | R LED                  |
| 2     | Write     | W LED                  |
| 3     | Auto      | R+W LEDs ("RW")        |

**[notes, readme]**

Legacy behavior, which the new bridge preserves exactly:

* **Activation** sets every strip to **Write (2)**, sends a snapshot, then sets
  everything to **Auto (3)**; the master and joystick SEL follow the same
  sequence. Result: console resets, faders at −∞, mutes off, R and W LEDs lit.
  **[readme, code]**
* **Strip SEL press** (console sends value 1): plugin rotates that strip's mode
  0→1→2→3→0 and sends the new mode back. **[code]**
* **Touch** (console sends 6) while mode is Auto (3): plugin answers **Write
  (2)**. **Release** (console sends 5) in Auto: plugin answers **Auto (3)**.
  Touch/release in other modes are ignored. **[code]**
* **Master SEL press** (ch5 CC64 = 1): plugin rotates the master mode and
  applies it to **all strips** (the master fader has no touch sensor). The
  master's own SEL address is *not* rewritten in the legacy code; preserve.
  **[code, readme]**
* Hardware limitation: touching more than 2–3 faders simultaneously does not
  always register. SEL buttons are the manual fallback. **[readme]**
* Console-side behavior of mode changes, and the meaning of the notes' cryptic
  `1+2+(CC#0, ch.1, val.0)=W` entry, remain **uncertain**; the legacy plugin's
  observed-in-practice rules above are what we preserve.

---

## 6. Values, quantization, direction

* Fader/joystick/master positions are 7-bit (0..127), mapped to normalized
  0.0..1.0 by `value / 127`. Outbound, normalized values are quantized to
  `round(v * 127)`. **[code]**
* Mute ON = 3, OFF = 2; anything that is not 3 is treated as OFF on input.
  **[code]**
* The plugin deduplicates: a value identical to the last one sent for a
  parameter is not re-sent. **[code]**

---

## 7. Ordering and timing observations

* No response/pacing requirements are documented for the console. The legacy
  plugin sent everything immediately, at the host's automation rate, with only
  per-parameter deduplication. The author observed that its TEST MODE needed
  frame-rate limiting "to avoid flooding the Cinemix MIDI buffers" (25 Hz per
  fader, 10 Hz per mute, all strips at once ≈ 1800 msg/s peak — beyond DIN
  capacity, apparently buffered/absorbed by the MIDI interface). **[code]**
* Outbound order matters for the mode/snapshot sequences (see Compatibility).
  The new bridge keeps management messages strictly ordered and paces position
  data behind them.

---

## 8. What the legacy plugin does NOT implement

There may be more automation surface on the console than the legacy bridge
used. Not implemented in the legacy plugin (and therefore not in this bridge
until hardware evidence appears): master-section mutes other than the
joysticks, EQ/dynamics controls, VCA/group masters, monitor controls, any
system-exclusive traffic. The notes record no sysex use at all.

---

## 9. Parameter ↔ MIDI table used by the legacy plugin

For the default profile (36 strips, LO=24 / HI=12, stereo strips 25–28), the
legacy parameter numbering is:

| Param IDs        | Meaning                                         |
| ---------------- | ----------------------------------------------- |
| 0..71            | Faders: per strip `2s` = Chan path, `2s+1` = Mix path (s = 0..35) |
| 72..143          | Mutes: `72+2s` = Chan, `73+2s` = Mix            |
| 144..153         | AUX 1..10 mutes                                 |
| 154,155          | Joystick 1 X, Y                                 |
| 156              | Joystick 1 mute                                 |
| 157,158          | Joystick 2 X, Y                                 |
| 159              | Joystick 2 mute                                 |
| 160              | Master fader                                    |

Fader param `p` → CC `2p` on ch 1 (p<48) or `2(p−48)` on ch 2 (p≥48), sent to
port 1 (p<48) or port 2 (p≥48). Mute param `p` → CC `p−72` ch 3 / `p−120`
ch 4, port 1 / port 2. This numbering is reproduced bit-for-bit by the new
bridge's default profile so that documents and AU parameter IDs line up with
the legacy plugin. **[code]**
