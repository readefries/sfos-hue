# Lampje

Native Philips Hue app for Sailfish OS. Work in progress.

Talks directly to the Hue bridge on the local network over the CLIP v2 API: rooms and zones,
single lights (brightness, colour temperature, colour), scenes, and live updates when lights are
changed elsewhere (wall switches, the official app, automations). The bridge's schedules (wake up,
go to sleep, timers, lights on a schedule) can be switched on and off, moved to another time or
other days, and deleted.

Zigbee lights and sensors paired with [zigbee2mqtt](https://www.zigbee2mqtt.io/) join in through
an MQTT broker (Settings, Zigbee): they can go into the bridge's rooms and zones, or into rooms of
Lampje's own, and switch along with the Hue lights there.

## Layout

| Path    | What |
|---------|------|
| `core/` | Static library, Qt Core/Gui/Network only, so it's testable on its own. |
| `app/`  | `harbour-lampje`, the Silica UI. Sailjail permission: `Internet`. |
| `tests/`| Qt Test suite for `core`, against an in-process fake bridge (`tests/common/fakehuebridge.h`) and MQTT broker (`fakemqttbroker.h`). |
| `tools/fakebridge.py` | A pretend bridge for the emulator or the phone; see below. |
| `tools/z2mdump.py` | Saves zigbee2mqtt's device list from a broker (`tools/z2mdump.py <host> [user]`), e.g. to see how a device exposes itself. |

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
  Lamps with several spots (e.g. Hue Runner) are separate devices with one light each; only
  their model ids (the lamp's 12NC plus `_01`, `_02`, ...) say they belong together. Within a room
  those become a fixture, with an id that works wherever a light id does.
- `HueRoomModel`, `HueLightModel`, `HueSceneModel`, `HueSensorModel`, `HueScheduleModel`: list models over the store. When a change
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
- `MqttClient`: the bit of MQTT 3.1.1 needed (Qt 5.6 has no QtMqtt): user name and password,
  subscribe, QoS 0, keep-alive, reconnecting with backoff.
- `Zigbee2Mqtt`: zigbee2mqtt's devices in the store as if the bridge had them, with ids
  `z2m:<ieee address>...`: each light or switch channel a `light`, sensors as `motion`,
  `light_level`, `temperature` and `device_power`, availability as `zigbee_connectivity`. Commands
  go to `<base>/<name>/set`, merged per 150 ms so a dragged slider doesn't flood the network.
- `LocalGroups`: rooms and zones of Lampje's own (`local:...`), and which rooms and zones Zigbee
  lights are in, kept in settings. The store adds those lights to the groups (`lightsOf()`), so a
  room can hold both: the bridge's lights switch through its grouped_light, the others one by one.

Sailfish OS ships Qt 5.6: don't use newer Qt APIs.

## Building

As in the other projects, with the Sailfish SDK (run from `/home/...`, not `/var/home/...`):

```sh
mkdir -p build-aarch64 && cd build-aarch64
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build ..     # tests run in %check
```

`scripts/deploy.sh [ssh-host]` builds and copies the RPM to the phone; `scripts/docker-build.sh
[release] [arch]` builds in the community SDK image, as CI does.

## Releasing

Commits follow [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `ci:`,
...). After every green build on `main`, CI runs [semantic-release](https://semantic-release.gitbook.io/):
it picks the next version from the commits since the last `vX.Y.Z` tag (`fix:` patch, `feat:` minor,
`BREAKING CHANGE:` major), builds the RPMs for aarch64, armv7hl and i486 with that version, and
creates the tag plus a GitHub release with the notes, the RPMs, `SHA256SUMS` and a build provenance
attestation. It commits nothing; the version in the spec is a placeholder that release builds
replace with the tag's.

### Installing a release from GitHub

Download `harbour-lampje-<version>-1.<arch>.rpm` for your device (`uname -m`: `aarch64` on recent
devices) and check where it came from before installing it:

```sh
sha256sum -c --ignore-missing SHA256SUMS
gh attestation verify harbour-lampje-<version>-1.aarch64.rpm -R readefries/sfos-hue  # built here, by CI
devel-su pkcon install-local ./harbour-lampje-<version>-1.aarch64.rpm
```

Packages installed this way don't update by themselves.

## Translations

The build runs lupdate over `app/` and `core/`, adding new strings to `app/translations/*.ts`.
`tools/translations.py` then fills them in from its tables (Dutch, and English for plural forms
only); strings it doesn't know stay unfinished and show in English. For another language, add a
table there and the file to `TRANSLATIONS` in `app/app.pro`.

## Without a bridge

```sh
tools/fakebridge.py            # http://0.0.0.0:8080
```

Then enter `http://10.0.2.2:8080` as the address in the emulator (or `http://<pc>:8080` on the
phone). It serves the resources in `tests/common/fixture.json`, applies changes (rooms follow
their lights, scenes set theirs) and sends the events. Pairing answers "press the button" twice
before it succeeds. `--cert`/`--key` serve HTTPS, to try the certificate pinning.

## Roadmap

- **Done since the first version:** lamps with several spots as one light; sensors and switches
  (motion, temperature, light level, buttons, batteries; motion sensors can be switched off); icons
  by lamp and room type; Dutch; creating, renaming, updating and deleting scenes; creating and editing zones;
  two configurable cover buttons; renaming lights, and renaming rooms or changing their kind;
  zigbee2mqtt lights and sensors, in rooms with the Hue lights or of their own; the bridge's
  schedules (`behavior_instance` from its automation scripts): on/off, time and days, deleting.
- **Next:** a fake MQTT broker in `tools/` for the emulator; MQTT over TLS; moving the bridge's
  lights between rooms.
- **Then:** motion sensor sensitivity; more than one bridge; storing the
  app key in Sailfish Secrets.
- **Maybe:** creating schedules, remote access through Philips' cloud API (needs a
  registered app), a background daemon for notifications from sensors.
- **Out of scope:** Entertainment areas / sync, firmware updates, adding new lights.
