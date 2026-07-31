# panel

The parent panel: one page, the robot's face on top and its settings
below.

These files are a **single source**. During development they are served
from disk; on the device the same files are compiled into the firmware
image with `EMBED_FILES`. They are never copied or forked, so the panel
seen while developing and the panel served by the robot cannot drift
apart. A firmware update carries the panel with it.

## Files

| File | Role |
|---|---|
| `index.html` | Page structure |
| `stil.css` | Styling |
| `pati.js` | Panel logic; talks to the robot over REST, or to the prototype over WebSocket |
| `gozler240.js` | Eye renderer — the reference implementation ported to C++ |
| `mikrofon.js` | Browser microphone capture (development only) |
| `ornek.js` | Sample data for bench mode |

`gozler240.js` writes pixels into a 240×240 buffer rather than using
canvas drawing commands, because the device renders the same geometry
without a graphics library. The two are compared pixel by pixel in the
host tests.

## Development

```
TELEFONDAN-INCELE.bat    serve on the local network (port 8756)
PAYLAS.bat               open an HTTPS tunnel to that server
```

Plain HTTP is enough to review layout, but browsers only grant
microphone access over HTTPS — the tunnel exists for that. `PAYLAS.bat`
refuses to open a tunnel when nothing is listening, since an empty
tunnel returns 502 and looks like a device fault.

## Bench mode

Opened without a robot, the page detects this and falls back to sample
data, marking itself with a banner so measurements are not mistaken for
real ones. With a robot present it polls `/api/durum` and switches to
live values.

The user interface is Turkish only.
