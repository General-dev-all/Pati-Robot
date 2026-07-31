# Pati

A voice companion robot for children, built on an ESP32-S3.

Pati runs no local model. Audio streams from the device to the Gemini
Live API over a raw WebSocket and returns to the speaker the same way.
No companion app, no intermediate server, no pairing step. Settings are
managed from a web panel served by the robot itself.

Firmware is complete and verified on hardware. Electronics assembly and
enclosure integration are in progress.

---

## Layout

```
firmware/    ESP-IDF v5.5 application
panel/       Parent panel — one web page, served from disk during
             development and compiled into the firmware for the device
prototype/   Python reference implementation; the host tests compare
             firmware behaviour against it
assembly/    Electronics assembly guide (self-contained HTML)
enclosure/   3D-printable parts and CAD source
```

`panel/` is a single source. The page is not duplicated for the device
build, so a firmware update carries the panel with it.

---

## Hardware

| Component | Notes |
|---|---|
| ESP32-S3 board, **N16R8** | 16 MB flash, 8 MB octal PSRAM. PSRAM is required. Boards sold under this name are often clones using a CH343 USB-UART bridge rather than a CP2102; the driver differs. |
| INMP441 | I2S MEMS microphone. **Supply from 3.3 V** — 5 V destroys the part. |
| MAX98357A | I2S class-D amplifier. Bridge-tied output; neither speaker terminal may be grounded. |
| Speaker, 8 Ω | 5 cm, 5 W |
| 1.3" IPS display, 240×240, ST7789 | 7-pin SPI variant; some modules expose no CS pin |
| Dupont jumper wires, male header strips | 2.54 mm |

Octal PSRAM occupies GPIO 35, 36 and 37 — present on the header, not
usable. Pin assignments have a single source:
[`firmware/main/pati_pinler.h`](firmware/main/pati_pinler.h).

Wiring, step by step with a test after each stage:
[`assembly/REHBER.html`](assembly/REHBER.html).

---

## Build

Requires ESP-IDF v5.5. On Windows use PowerShell — `idf_tools.py`
refuses to run under MSYS.

```
cp firmware/sdkconfig.defaults.local.ornek firmware/sdkconfig.defaults.local
# fill in Wi-Fi credentials

. "$IDF_PATH/export.ps1"
cd firmware
idf.py -p <PORT> flash monitor
```

`firmware/KARTA-YUKLE.bat` does the same on Windows and finds the
serial port itself.

No API key is built in. The Gemini key is entered from the parent panel
and stored on the device, so the image contains no secret and can be
published. The partition table is custom
([`firmware/partitions.csv`](firmware/partitions.csv)) and must be
written over USB — `idf.py flash`, not `app-flash`.

## Updates

Raise the version in [`firmware/surum.txt`](firmware/surum.txt) — first
line the number, the lines under it the sentence the parent reads — and
push. GitHub builds the firmware and publishes a release holding
`pati.bin` and a generated `surum.json`; the device polls the latter and
downloads the former. No update server, no companion app, nothing to
upload by hand. Details in
[`firmware/SURUM-NASIL-CIKARILIR.md`](firmware/SURUM-NASIL-CIKARILIR.md).

The manifest is a release asset rather than a committed file because a
push publishes a committed file immediately but does not build the
binary. For the minutes in between, the panel would offer an update that
404s. Sharing a release makes the manifest unable to exist before the
image it describes.

The binary is not committed. The panel is compiled into it, so a panel
change ships as a firmware update — which is why the two cannot drift.

The build needs no secrets, which is what makes CI possible at all: the
key and the Wi-Fi credentials both live in the device's NVS, and no
source file reads them from the configuration.

Rollback is enabled: a new image boots on trial and is only marked good
once the network stack and panel are up. An image that cannot get that
far is reverted by the bootloader on the next power cycle.

---

## Tests

```
firmware/test/derle.bat     C++ eye renderer vs. the browser renderer,
                            pixel by pixel; C++ memory engine vs. Python
python prototype/testler.py prototype behaviour
node panel/panel_test.mjs   the panel loads against a stub DOM, in each
                            of the four sample data sets
```

The panel check exists because a missing element makes `querySelector`
return null, and the resulting throw stops the module — so the failure
is not the card that was edited, it is every card below it. Nothing on
screen says why.

The host comparisons exist because a port that compiles is not a port
that behaves identically. Generated headers are produced by
`prompt_uret.py` and `goz_uret.mjs`, which read their output back and
compare it to the source; they are not edited by hand.

---

## Operation

With no reachable saved network the device opens its own access point
and serves a captive portal for Wi-Fi setup. Once connected it
advertises itself over mDNS and the panel is reachable at `pati.local`.

The panel covers voice and rate, response latency, sleep threshold,
usage and estimated cost, the robot's name, the facts it has retained,
the Gemini key, and firmware updates. The interface is Turkish only.

Setup is Wi-Fi, then the key. Until a key is entered the robot has a
face but no voice, and the panel says so rather than leaving the parent
to guess.

When Google rejects a request the panel distinguishes the cases, because
the parent's next action differs: a rejected key needs replacing, an
exhausted quota needs credit, and an unreachable network needs nothing
at all. Collapsing the three would invite paying for an outage.

---

## Privacy

No child data is committed here. API keys, Wi-Fi credentials, retained
memory, usage counters and transcripts are excluded by `.gitignore`.

On the device, memory is stored in NVS and written atomically, since
the expected shutdown is a power cut. It can be cleared from the panel.

---

## License

MIT — see [LICENSE](LICENSE), which also lists third-party components.
