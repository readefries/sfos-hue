# Lampje

Native Philips Hue app for Sailfish OS. Work in progress.

Talks directly to the Hue bridge on the local network over the CLIP v2 API: rooms and zones,
single lights (brightness, colour temperature, colour), scenes, and live updates when lights are
changed elsewhere (wall switches, the official app, automations).

## Layout

| Path    | What |
|---------|------|
| `core/` | Static library, Qt Core/Gui/Network only, so it's testable on its own. |
| `app/`  | `harbour-lampje`, the Silica UI. Sailjail permission: `Internet`. |
| `tests/`| Qt Test suite for `core`, against an in-process fake bridge (`tests/common/fakehuebridge.h`). |
| `tools/fakebridge.py` | A pretend bridge for the emulator or the phone; see below. |

What's in `core/`:

- `HueClient`: what QML sees as `hue`. Remembers the paired bridge, exposes connection and
  pairing state, and takes commands. A command updates the store right away (so sliders don't lag
  behind) and goes to the bridge through `HueCommandQueue`; the event stream then confirms it.
  When the bridge rejects one, everything is loaded again.
- `HueBridge`: the connection. Opens `/eventstream/clip/v2`, then loads `/clip/v2/resource` into
  the store (holding back events that arrive meanwhile), and from then on applies events. When the
  stream ends it reconnects with backoff and loads everything again. 401/403 and a changed
  certificate are final: pair again.
- `HueResourceStore`: every v2 resource by id, plus the relations the UI needs (room → devices →
  lights, zone → lights, room → grouped_light, light → device → zigbee_connectivity, scenes of a
  group).
- `HueRoomModel`, `HueLightModel`, `HueSceneModel`: list models over the store. When a change
  doesn't add, remove or reorder rows, only the rows it touches are updated, so delegates (and a
  slider under the finger) survive the stream's updates.
- `HueCommandQueue`: about 10 light commands a second, and at most one a second per group, as
  Philips asks. Waiting commands for the same resource are merged, so a dragged slider sends its
  latest value instead of a backlog.
- `HueDiscovery`: mDNS (`_hue._tcp.local`, queried from an ephemeral port so the answers come back
  directly, without sharing port 5353), Philips' discovery service (rate limited), or an address
  typed in. Each candidate is asked who it is via `/api/0/config`.
- `HuePairing`: `POST /api` until the link button is pressed.
- `HueTls`: the bridge's certificate is signed by Signify's own CA and named after the bridge id,
  so the usual checks always fail. It is pinned (SHA-256) when pairing and must match afterwards.
- `HueColor`: sRGB ↔ CIE xy, clamping to each lamp's gamut, mirek → display colour.

Sailfish OS ships Qt 5.6: don't use newer Qt APIs.

## Building

As in the other projects, with the Sailfish SDK (run from `/home/...`, not `/var/home/...`):

```sh
mkdir -p build-aarch64 && cd build-aarch64
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build ..     # tests run in %check
```

`scripts/deploy.sh [ssh-host]` builds and copies the RPM to the phone; `scripts/docker-build.sh
[release] [arch]` builds in the community SDK image, as CI does.

## Without a bridge

```sh
tools/fakebridge.py            # http://0.0.0.0:8080
```

Then enter `http://10.0.2.2:8080` as the address in the emulator (or `http://<pc>:8080` on the
phone). It serves the resources in `tests/common/fixture.json`, applies changes (rooms follow
their lights, scenes set theirs) and sends the events. Pairing answers "press the button" twice
before it succeeds. `--cert`/`--key` serve HTTPS, to try the certificate pinning.

## Roadmap

- **Next:** run it on a device against a real bridge (the parts the tests can't cover: mDNS inside
  Sailjail, the real certificate, the stream across suspend); room and light icons by archetype;
  translations.
- **Then:** creating and editing scenes; sensors (motion, temperature, battery); renaming lights and
  rooms; more than one bridge; storing the app key in Sailfish Secrets.
- **Maybe:** automations (`behavior_instance`), remote access through Philips' cloud API (needs a
  registered app), a background daemon for notifications from sensors.
- **Out of scope:** Entertainment areas / sync, firmware updates, adding new lights.
