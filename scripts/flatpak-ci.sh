#!/bin/sh
# Builds the desktop app's Flatpak on a CI runner (Ubuntu), tests included.
#
#   scripts/flatpak-ci.sh              build and test; dist/Lampje-<commit>.flatpak, unsigned, to try
#   scripts/flatpak-ci.sh 1.2.3        a release: also, signed,
#       site/repo/                     the Flatpak repo for GitHub Pages, which `flatpak update` follows
#       site/lampje.flatpakref         what `flatpak install --user <url>` takes
#       site/lampje.flatpakrepo        the repo on its own, for `flatpak remote-add`
#       dist/Lampje-1.2.3.flatpak      a single-file bundle for the GitHub release; installing it
#                                      also adds the repo, so it updates from there afterwards
#
# A release signs with FLATPAK_GPG_KEY (an armoured secret signing subkey, without passphrase),
# whose fingerprint is FLATPAK_GPG_KEY_ID; flatpak/signing-key.asc is its public key.
set -eu

version=${1:-}
case "$version" in
    ''|[0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "version must look like 1.2.3, not '$version'" >&2; exit 2 ;;
esac
site_url=${FLATPAK_SITE_URL:-https://readefries.github.io/sfos-hue}
app=nl.readefries.Lampje
manifest=flatpak/$app.yml
flathub=https://dl.flathub.org/repo/flathub.flatpakrepo

cd "$(dirname "$0")/.."

sudo=$([ "$(id -u)" = 0 ] || echo sudo)
if ! command -v flatpak-builder >/dev/null; then
    $sudo apt-get update -qq
    $sudo apt-get install -y -qq flatpak flatpak-builder >/dev/null
fi
# Ubuntu 24.04 doesn't let unprivileged processes make user namespaces, which bubblewrap needs.
if [ "$(sysctl -n kernel.apparmor_restrict_unprivileged_userns 2>/dev/null || echo 0)" = 1 ]; then
    $sudo sysctl -q -w kernel.apparmor_restrict_unprivileged_userns=0
fi
flatpak remote-add --user --if-not-exists flathub "$flathub"

# No FUSE on the runners.
build="flatpak-builder --user --install-deps-from=flathub --force-clean --disable-rofiles-fuse
       --state-dir=.flatpak-builder"

if [ -z "$version" ]; then
    # Something to try from any branch: a bundle without signature or repo to update from.
    $build --repo=repo flatpak-build "$manifest"
    rm -rf dist
    mkdir -p dist
    name="Lampje-$(git rev-parse --short HEAD 2>/dev/null || echo dev).flatpak"
    flatpak build-bundle --runtime-repo="$flathub" repo "dist/$name" $app
    ls -l dist
    exit 0
fi

: "${FLATPAK_GPG_KEY:?the signing key}" "${FLATPAK_GPG_KEY_ID:?the signing key's fingerprint}"

# The version goes into the app (About, --version) and its metainfo, as the RPM build does with
# the spec. The checkout is CI's own, so changing it in place is fine.
sed -i "s/qmake PREFIX=\${FLATPAK_DEST}/& VERSION=$version/" "$manifest"
grep -q "VERSION=$version" "$manifest"
sed -i "s|<release version=\"[^\"]*\" date=\"[^\"]*\"/>|<release version=\"$version\" date=\"$(date -u +%F)\"/>|" \
    desktop/$app.metainfo.xml
grep -q "release version=\"$version\"" desktop/$app.metainfo.xml

GNUPGHOME=$(mktemp -d)
export GNUPGHOME
trap 'rm -rf "$GNUPGHOME"' EXIT
printf '%s\n' "$FLATPAK_GPG_KEY" | gpg --batch --quiet --import
sign="--gpg-sign=$FLATPAK_GPG_KEY_ID --gpg-homedir=$GNUPGHOME"

$build --default-branch=stable --repo=repo $sign flatpak-build "$manifest"
flatpak build-update-repo $sign --title=Lampje --default-branch=stable \
    --generate-static-deltas --prune repo

# What clients check the repo's signatures with.
gpg --dearmor < flatpak/signing-key.asc > signing-key.gpg
key=$(base64 -w0 signing-key.gpg)

rm -rf site dist
mkdir -p site dist
cp -r repo site/repo
cat > site/lampje.flatpakrepo <<EOF
[Flatpak Repo]
Title=Lampje
Url=$site_url/repo/
Homepage=https://github.com/readefries/sfos-hue
Comment=Philips Hue lights, on the desktop
GPGKey=$key
EOF
cat > site/lampje.flatpakref <<EOF
[Flatpak Ref]
Title=Lampje
Name=$app
Branch=stable
Url=$site_url/repo/
IsRuntime=false
SuggestRemoteName=lampje
GPGKey=$key
RuntimeRepo=$flathub
EOF
cat > site/index.html <<EOF
<!doctype html>
<meta charset="utf-8">
<title>Lampje</title>
<p>Lampje $version, the Philips Hue app for the Linux desktop. Install it with
<pre>flatpak install --user $site_url/lampje.flatpakref</pre>
<p>and <code>flatpak update</code> keeps it up to date. Source and releases:
<a href="https://github.com/readefries/sfos-hue">github.com/readefries/sfos-hue</a>.
EOF

flatpak build-bundle --runtime-repo="$flathub" --repo-url="$site_url/repo/" \
    --gpg-keys=signing-key.gpg repo "dist/Lampje-$version.flatpak" $app stable
ls -l dist site
