# User Guide — hooking up the console and running automation

Everything here about the physical console comes from the legacy bridge's
documentation (GSi, 2012) and is marked **[legacy-verified]** — it has not
been re-verified on the target console by this project.

## 1. The cable **[legacy-verified]**

The Cinemix's automation MIDI lives on the console's **ASYNC INTERFACE DB25**
connector — not on DIN sockets. Build a DB25 (male) → 4× DIN5 (male) cable
(pinout identical for a DB9 if you keep the PowerVCA's DB9 cable and make an
adapter; schematics book page 34, "AS Con./DigPower"):

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

Pair 1 drives the **LO** side (first 24 strips); pair 2 drives the **HI** side
(strips 25+ and the master section). Find your console's split: press **SETUP**
then the left arrow — the legacy author's console reads `LO=24 HI=12 MSTR=hi`.
The MIDI interface needs **two inputs and two outputs**.

Cable check: send `CC#127 = 127` on **channel 5** to **both outputs** from any
MIDI tool. The console enters **remote-control mode** (joysticks stop driving
the X/Y LEDs). Release with a single `0xFF` byte or by power-cycling.

## 2. Installing and loading

`make install` in `mac/`, restart Logic, load **Cinemix Automation Bridge**
as an instrument (no MIDI track needed). Open the plugin panel and select:

* **MIDI In 1 / In 2** — the interface ports wired to console pairs 1/2;
* **MIDI Out 1 / Out 2** — the same pairs, output side.

Selections persist in the user defaults and are restored by name.

## 3. Activating

Click **Activate**. The bridge runs the legacy-proven sequence: remote mode →
every strip to WRITE → full snapshot → every strip to AUTO (R+W LEDs on).
Expect: console resets, faders to −∞, mutes off, R and W LEDs lit.

**The console is not touched until you click Activate**, and the bridge
releases it (full legacy deactivation sequence, `0xFF` last) whenever the
plugin is removed or Logic closes — never unplug the MIDI cable while the
console is in remote mode; deactivate first.

## 4. Automation workflow (Logic)

1. Enable automation on the instrument track (Touch/Latch/Write).
2. Touch a fader → the console's touch sensor arms that fader for writing
   (the bridge answers WRITE; R+W on release) → move it. The move is reported
   to Logic with begin/change/end gesture events, so Logic records it.
3. Playback: Logic's automation drives the motor faders through the bridge.
   The bridge suppresses motor echoes (feedback-loop prevention) and paces
   outbound MIDI (500 msg/s default) so the DIN link cannot be flooded.
4. **Send Snapshot** at the start of the song (an empty bar) to store the
   initial console state — only used automation is otherwise stored.
5. Hardware note **[legacy-verified]**: touching more than 2–3 faders at once
   does not always register; use the strip SEL buttons to force
   READ/WRITE/AUTO per strip (press = rotate ISO→READ→WRITE→AUTO). The master
   SEL sets the mode of **all** strips at once (the master fader has no touch
   sensor and is not motorized).

## 5. Buttons

| Button | Action |
| --- | --- |
| Activate / Deactivate | enter / release console remote mode |
| Send Snapshot | re-send the full parameter state (and write it into Logic automation where recording) |
| Reset All | all faders to −∞, mutes off, master to max, everything to AUTO |
| All Mutes | toggle every mute (channel, AUX, joysticks) |
| Test Mode | demo animation (25 Hz fader / 10 Hz mute ramp) — MIDI only, never writes automation |

## 6. Console configuration (mixer profile)

The bridge defaults to the legacy-proven console layout: **LO=24, HI=12,
strips 25–28 = stereo inputs S1..S4, 2 joysticks, 10 AUX mutes, master
fader on the HI side**, with the legacy parameter numbering (0..160).

A different console is described by a property list at
`~/Library/Application Support/CinemixAutomationBridge/profile.plist`:

```xml
<plist version="1.0"><dict>
  <key>name</key><string>My Cinemix</string>
  <key>loStrips</key><integer>24</integer>
  <key>hiStrips</key><integer>8</integer>
  <key>stereoStrips</key><array><integer>24</integer></array>
  <key>hasJoystick1</key><true/>
  <key>hasJoystick2</key><true/>
  <key>auxMuteCount</key><integer>10</integer>
  <key>hasMasterFader</key><true/>
  <key>faderResolution</key><string>sevenBit</string>   <!-- or fourteenBit (unverified) -->
  <key>echoHysteresisSteps</key><integer>2</integer>
  <key>budgetMessagesPerSecond</key><integer>500</integer>
</dict></plist>
```

Remove the file to return to the default. Only LO=24/HI=12 has ever been
hardware-proven; any other split is at your own risk (brief §32/§33).

## 7. Diagnostics

The Log pane shows activation/deactivation, port state, unknown console CCs
(raw bytes), throttled traffic and errors. Set the level to **MIDI in/out**
to watch every byte. Unknown CCs are logged, not silently dropped
(brief §22).
