# Meshy for Haiku

A native [Haiku](https://www.haiku-os.org/) client for [MeshCore](https://meshcore.io/)
mesh radios, ported from [Meshy](https://codeberg.org/sesivany/meshy) by
Jiří Eischmann and contributors.

Upstream Meshy is Python and GTK4/libadwaita. This port is pure C++ with a
native Haiku Interface Kit UI and no Python, GTK or GLib at runtime. The
MeshCore protocol, crypto, storage and transports were reimplemented in C++
and verified against upstream's own implementation (see **Verification**).

Licensed under the GNU GPL v3 or later, the same as upstream.

## Status

The core is complete and tested. The UI is written but **has not yet been
compiled on Haiku** — it was developed on a Linux host, so expect a round of
build fixes on first build. Everything below the UI runs and is tested today.

| Layer | State |
|---|---|
| Protocol: 70 frame builders, 30 parsers, SMAZ, Cayenne LPP | Done, differentially verified |
| Crypto: SHA-256, HMAC, AES-128-ECB, GroupText, channel hashes | Done, known-answer tested |
| Storage: SQLite, upstream-compatible schema | Done, round-trip tested |
| Transports: USB/UART serial, TCP | Done, framing tested over loopback |
| Engine: sync, messaging, acks, retries, routing | Done, tested against a mock device |
| Haiku UI: 10 views | Written, needs a first build on Haiku |

## Building

On Haiku:

```
make
./build/Meshy      # or: make install
```

The makefile uses Haiku's `makefile-engine` when present and falls back to a
plain `g++` build otherwise.

Dependencies, all in HaikuPorts:

```
pkgman install openssl3_devel sqlite_devel
```

To run the core test suite (works on Haiku and on Linux, no Haiku headers
needed):

```
make tests
```

## Design

```
src/core/     portable C++17, no Haiku dependency — builds and tests on Linux
  Bytes       bounds-checked readers/writers with BytesIO-compatible semantics
  Crypto      SHA-256, HMAC-SHA-256, AES-128-ECB (OpenSSL)
  Smaz        SMAZ decompression for short messages
  Protocol    frame builders and parsers, Cayenne LPP
  MeshPacket  raw packet decode, channel crypto, region codes
  Models      Contact, Message, Channel, stable message identity
  Storage     SQLite persistence
  Transport   framing, serial and TCP
  Engine      state machine: connect, sync, send, ack, retry, route

src/haiku/    Interface Kit UI
  MeshyApp, MainWindow, EngineBridge, and ten views
```

The engine owns every piece of protocol state on a single worker thread.
Public methods enqueue a command and return; results come back through an
`EngineListener`. `EngineBridge` turns those callbacks into `BMessage`s posted
to the window, so all UI work happens on the window thread with no locking.

Upstream keeps its controllers as mixins over the GTK window — the frame
handler alone reaches 62 distinct attributes of it — which is why this port
separates the engine from the UI rather than translating that structure.

## What differs from upstream

**No Bluetooth LE.** Haiku has no BLE stack, so connections are USB/UART
serial or TCP (a Wi-Fi bridge). This is the one real feature gap.

**The map has no tiles.** `libshumate` is not available, so the map view draws
nodes on a projection with a graticule, a distance scale and links to your own
node, rather than over street tiles.

**Transcripts, not bubbles.** Chat and channel history render in a styled
read-only `BTextView`. It wraps and scrolls correctly for free and the text
stays selectable and copyable.

**Serial I/O uses POSIX termios** rather than `BSerialPort`, so the same code
path runs on Haiku and on Linux and can be tested on either. Device
enumeration is platform-specific (`/dev/ports` on Haiku).

Databases are interchangeable with upstream: a `meshy.db` copied from a Linux
install opens unchanged, and the older-database migrations are applied on
open.

## Verification

The protocol layer is checked against upstream's actual Python, not against a
reading of it. `tests/diffgen.cpp` and `tests/diffgen.py` generate the same
1,407 deterministic cases — every builder, every parser, path and scope
helpers, SMAZ, Cayenne LPP, plus fuzzed and truncated frames — and the outputs
are compared byte for byte.

```
make tests                                   # core suite
tests/run_diff.sh /path/to/meshy-upstream    # differential vs upstream
```

| Suite | Checks |
|---|---|
| Differential vs upstream Python | 1,407 cases, identical |
| Crypto known-answer (FIPS-197, RFC-4231, FIPS 180-4) | 30 |
| Storage round-trip + schema vs upstream-created DB | 60 |
| Transport framing over loopback TCP | 22 |
| Engine against a mock MeshCore device | 34 |

The differential found four behaviours that reading the source would not have
revealed: upstream's `BytesIO` short-reads past the end of a frame rather than
failing; three different NUL conventions across frame types; `build_anon_req`
writes the requested hop count even when the path holds fewer hops; and
base64 silently discards invalid characters on its lenient retry path. All
four are matched.

`tests/haikumock/` holds minimal stand-in Haiku headers used only to
type-check the UI on a non-Haiku host. They mirror the API surface this
project uses and are **not** an API reference; they catch our own mistakes
(bad signatures, missing includes), not Haiku API drift.

## Credits

Meshy was created by Jiří Eischmann and the Meshy contributors. This port
follows their protocol implementation closely and owes everything to it.
