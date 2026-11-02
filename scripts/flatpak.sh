#!/bin/sh
# Builds the desktop app as a Flatpak and installs it for this user (tests included).
#
#   scripts/flatpak.sh            then: flatpak run nl.readefries.Lampje
#
# Needs Flathub's flatpak-builder and KDE runtime once:
#   flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
#   flatpak install --user flathub org.flatpak.Builder org.kde.Sdk//6.11 org.kde.Platform//6.11
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
# Outside the source tree, so the build never copies its own leftovers.
work=${XDG_CACHE_HOME:-$HOME/.cache}/lampje-flatpak

# The builder asks the host's flatpak for the SDK with its own sandboxed XDG_DATA_HOME, where no
# user installation is; FLATPAK_USER_DIR wins over that.
flatpak run --env=FLATPAK_USER_DIR="${FLATPAK_USER_DIR:-$HOME/.local/share/flatpak}" \
    --command=flatpak-builder org.flatpak.Builder \
    --user --install --force-clean --ccache \
    --state-dir="$work/state" "$work/build" "$repo/flatpak/nl.readefries.Lampje.yml"
echo
echo "Installed. Start it from the app menu, or: flatpak run nl.readefries.Lampje"
