# Lampje

Native Philips Hue app for Sailfish OS, and for the Linux desktop (Kirigami, as a Flatpak). Work
in progress.

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
| `desktop/` | `lampje`, the Kirigami UI for the desktop (Qt 6), sharing `app/`'s JS helpers and icons. |
| `flatpak/` | The desktop app's Flatpak manifest, on KDE's runtime. |
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

Sailfish OS ships Qt 5.6: don't use newer Qt APIs. The desktop builds the same core with Qt 6, so
it has to compile with both (a few `QT_VERSION` checks).

## Building

As in the other projects, with the Sailfish SDK (run from `/home/...`, not `/var/home/...`):

```sh
mkdir -p build-aarch64 && cd build-aarch64
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build ..     # tests run in %check
```

`scripts/deploy.sh [ssh-host]` builds and copies the RPM to the phone; `scripts/docker-build.sh
[release] [arch]` builds in the community SDK image, as CI does.

### Desktop

`harbour-lampje.pro` builds the desktop app instead of the Silica one where there's no Sailfish SDK
but Qt 6 with Qt Quick Controls (and, at run time, Kirigami):

```sh
mkdir build-desktop && cd build-desktop
qmake6 .. && make -j8 && desktop/lampje
```

As a Flatpak, installed for your user (the first time needs Flathub's builder and KDE runtime; the
script says how):

```sh
scripts/flatpak.sh
flatpak run nl.readefries.Lampje
```

It pairs as `lampje#<computer name>`, separately from the phone, and keeps its settings under
`~/.var/app/nl.readefries.Lampje/` (outside the Flatpak: `~/.config/nl.readefries/lampje/`). The
desktop has no cover, so no cover buttons; the rest is as on the phone, with rooms, a room and a
light side by side when the window is wide enough. Outside Plasma it follows the desktop's dark or light
preference (from the portal, e.g. GNOME's or Sway's `color-scheme` setting) with Breeze Dark or
Light, also when it changes while running.

## Releasing

Commits follow [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `ci:`,
...). After every green build on `main`, CI runs [semantic-release](https://semantic-release.gitbook.io/):
it picks the next version from the commits since the last `vX.Y.Z` tag (`fix:` patch, `feat:` minor,
`BREAKING CHANGE:` major), builds the RPMs for aarch64, armv7hl and i486 and the desktop Flatpak
with that version, and creates the tag plus a GitHub release with the notes, the RPMs, the Flatpak
bundle, `SHA256SUMS` and a build provenance attestation; then it publishes the signed Flatpak repo
to GitHub Pages. It commits nothing; the versions in the spec and the metainfo are placeholders that
release builds replace with the tag's.

### Installing a release from GitHub

Download `harbour-lampje-<version>-1.<arch>.rpm` for your device (`uname -m`: `aarch64` on recent
devices) and check where it came from before installing it:

```sh
sha256sum -c --ignore-missing SHA256SUMS
gh attestation verify harbour-lampje-<version>-1.aarch64.rpm -R readefries/sfos-hue  # built here, by CI
devel-su pkcon install-local ./harbour-lampje-<version>-1.aarch64.rpm
```

Packages installed this way don't update by themselves.

### Installing the desktop app

Each release also publishes the desktop app to a Flatpak repo on GitHub Pages, signed with a
subkey of `85D2F114051209CAF1D750BF4236848D7A0A3403` kept for this (`flatpak/signing-key.asc`;
CI has it as the `FLATPAK_GPG_KEY` secret). Install it once and `flatpak update` keeps it current:

```sh
flatpak install --user https://readefries.github.io/sfos-hue/lampje.flatpakref
```

The release also has `Lampje-<version>.flatpak`, covered by `SHA256SUMS` and the attestation like
the RPMs; `flatpak install --user ./Lampje-<version>.flatpak` installs it and adds the same repo
for updates. A build from `scripts/flatpak.sh` installs as branch `master`, next to a release's
`stable`.

## Translations

The build runs lupdate over `app/` and `core/`, adding new strings to `app/translations/*.ts`. The
desktop's `desktop/translations/*.ts` are updated by hand, with Qt 6's lupdate:

```sh
lupdate-qt6 -locations relative desktop/qml app/qml/components/{archetypes,icons,schedules}.js core \
    -ts desktop/translations/lampje-en.ts desktop/translations/lampje-nl.ts
```

`tools/translations.py` then fills both in from its tables (Dutch, and English for plural forms
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

- **Done since the first version:** a desktop version (Kirigami, Flatpak); lamps with several spots as one light; sensors and switches
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
