# prototype

Python reference implementation. It runs the same conversation loop as
the firmware, on a PC, using the computer's microphone and speaker:

```
microphone → 16 kHz PCM → WSS → Gemini Live → 24 kHz PCM → speaker
```

No SDK, no browser, no audio processing in between — the transport
matches the device, so a number measured here means something there.

## Why it still exists

It is the behavioural reference for the firmware. The host tests compare
the C++ memory engine against `hafiza.py` check by check; a divergence
means one of the two is wrong. Prompt text and extraction rules are read
from here and generated into C++ headers rather than copied by hand.

The timing instrumentation also lives here. Latency is recorded per
turn, and the median/p90 arithmetic is duplicated on the device and
self-tested at boot so that the two remain comparable.

## Setup

```
python -m venv .venv
.venv/Scripts/python -m pip install -r requirements.txt
```

Then place a Gemini API key in `anahtar.txt`. That file, retained
memory, usage counters and transcripts are excluded from version
control.

## Use

```
python pati.py                              conversation; measurements
                                            written to olcumler/
python pati.py --arayuz --sayfa pati --disa serve the parent panel on
                                            the local network
python testler.py                           359 tests
```

`testler.py` covers the measurement tooling and the memory logic, not
the model. It answers "is this instrument reading correctly", not "is
the robot any good".

## Panel development

`../panel/TELEFONDAN-INCELE.bat` runs the second command above and
prints a LAN address. `../panel/PAYLAS.bat` opens an HTTPS tunnel to
it, which browsers require before granting microphone access.

The panel served here and the panel served by the robot are the same
files.
