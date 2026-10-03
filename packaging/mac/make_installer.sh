#!/usr/bin/env bash
#
# Builds a double-click macOS installer (.pkg) from a finished Release build.
#
#   ./packaging/mac/make_installer.sh [build-dir]
#
# Output: dist/BandTrigger-<version>-mac.pkg
#
# What it installs (each one can be ticked or unticked under "Customize"):
#   VST3 plug-ins    -> /Library/Audio/Plug-Ins/VST3          (on by default)
#   Standalone apps  -> /Applications/BandTrigger             (on by default)
#   Audio Units      -> /Library/Audio/Plug-Ins/Components    (off by default:
#                       AU effects can't send MIDI in most hosts)
#
# Signing:
#   By default the plug-ins are ad-hoc signed, which Apple Silicon requires and
#   which is enough to run on your own Mac. For an installer that opens
#   without any Gatekeeper warning, set these (needs a paid Apple Developer
#   account):
#     MAC_APP_SIGN_IDENTITY        "Developer ID Application: Your Name (TEAMID)"
#     MAC_INSTALLER_SIGN_IDENTITY  "Developer ID Installer: Your Name (TEAMID)"
#     APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD   (for notarization)

set -euo pipefail

BUILD_DIR="${1:-build}"
VERSION="${VERSION:-0.1.0}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT/dist}"
APP_SIGN="${MAC_APP_SIGN_IDENTITY:--}"     # "-" means ad-hoc
PKG_SIGN="${MAC_INSTALLER_SIGN_IDENTITY:-}"
PKG_NAME="BandTrigger-${VERSION}-mac.pkg"

[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$ROOT/$BUILD_DIR"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT_DIR" "$WORK/pkgs" "$WORK/resources" "$WORK/scripts-au"

log() { printf '\n==> %s\n' "$*"; }

# Finds a built bundle, e.g. bundle BandTriggerInst VST3 vst3
bundle() {
    local dir="$BUILD_DIR/$1_artefacts/Release/$2"
    local found
    found="$(find "$dir" -maxdepth 1 -name "*.$3" -print -quit 2>/dev/null || true)"
    if [[ -z "$found" ]]; then
        echo "error: no .$3 found in $dir (did the Release build finish?)" >&2
        exit 1
    fi
    printf '%s' "$found"
}

sign() {
    if [[ "$APP_SIGN" == "-" ]]; then
        codesign --force --deep --sign - "$1"
    else
        codesign --force --deep --options runtime --timestamp --sign "$APP_SIGN" "$1"
    fi
    codesign --verify --deep --strict "$1"
}

# stage <bundle> <install dir inside the package root>
stage() {
    local src="$1" dest="$2"
    mkdir -p "$dest"
    ditto "$src" "$dest/$(basename "$src")"
    sign "$dest/$(basename "$src")"
    echo "  staged $(basename "$src")"
}

# component <name> <root> [scripts dir]
component() {
    local name="$1" root="$2" scripts="${3:-}"
    local plist="$WORK/$name.plist"

    pkgbuild --analyze --root "$root" "$plist" >/dev/null
    # Always install to the stated folder. Without this, macOS "relocates" the
    # update to wherever it finds an older copy with the same bundle ID.
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
        i=$((i + 1))
    done

    local args=(--root "$root" --component-plist "$plist" --identifier "com.bandtrigger.pkg.$name"
                --version "$VERSION" --install-location /)
    [[ -n "$scripts" ]] && args+=(--scripts "$scripts")
    pkgbuild "${args[@]}" "$WORK/pkgs/$name.pkg" >/dev/null
    echo "  built $name.pkg"
}

#------------------------------------------------------------------------------
log "Staging and signing (identity: $APP_SIGN)"

VST3_DIR="$WORK/root-vst3/Library/Audio/Plug-Ins/VST3"
AU_DIR="$WORK/root-au/Library/Audio/Plug-Ins/Components"
APP_DIR="$WORK/root-app/Applications/BandTrigger"

for target in BandTrigger BandTriggerInst; do
    stage "$(bundle "$target" VST3 vst3)" "$VST3_DIR"
    stage "$(bundle "$target" AU component)" "$AU_DIR"
    stage "$(bundle "$target" Standalone app)" "$APP_DIR"
done

#------------------------------------------------------------------------------
log "Building component packages"

# Make macOS notice the new Audio Units without a restart.
cat > "$WORK/scripts-au/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
EOF
chmod +x "$WORK/scripts-au/postinstall"

component vst3 "$WORK/root-vst3"
component au   "$WORK/root-au" "$WORK/scripts-au"
component app  "$WORK/root-app"

#------------------------------------------------------------------------------
log "Building installer"

cp "$HERE/welcome.html" "$HERE/conclusion.html" "$WORK/resources/"

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>BandTrigger ${VERSION}</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <os-version min="11.0"/>

    <choices-outline>
        <line choice="vst3"/>
        <line choice="app"/>
        <line choice="au"/>
    </choices-outline>

    <choice id="vst3" title="VST3 plug-ins"
            description="BandTrigger (audio effect) and BandTrigger Instrument (for Ableton Live). This is the one you want.">
        <pkg-ref id="com.bandtrigger.pkg.vst3"/>
    </choice>
    <choice id="app" title="Standalone apps"
            description="Run BandTrigger on its own, outside a DAW, to try it with your audio interface. Installed in Applications/BandTrigger.">
        <pkg-ref id="com.bandtrigger.pkg.app"/>
    </choice>
    <choice id="au" title="Audio Unit plug-ins" start_selected="false"
            description="Optional. Audio Unit effects can't send MIDI in Logic or Ableton, so most people should skip this and use the VST3.">
        <pkg-ref id="com.bandtrigger.pkg.au"/>
    </choice>

    <pkg-ref id="com.bandtrigger.pkg.vst3" version="${VERSION}" onConclusion="none">vst3.pkg</pkg-ref>
    <pkg-ref id="com.bandtrigger.pkg.app"  version="${VERSION}" onConclusion="none">app.pkg</pkg-ref>
    <pkg-ref id="com.bandtrigger.pkg.au"   version="${VERSION}" onConclusion="none">au.pkg</pkg-ref>
</installer-gui-script>
EOF

PB_ARGS=(--distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK/pkgs")
[[ -n "$PKG_SIGN" ]] && PB_ARGS+=(--sign "$PKG_SIGN" --timestamp)
rm -f "$OUT_DIR/$PKG_NAME"
productbuild "${PB_ARGS[@]}" "$OUT_DIR/$PKG_NAME"

#------------------------------------------------------------------------------
if [[ -n "$PKG_SIGN" && -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
    log "Notarizing (this can take a few minutes)"
    xcrun notarytool submit "$OUT_DIR/$PKG_NAME" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" --wait
    xcrun stapler staple "$OUT_DIR/$PKG_NAME"
else
    echo "  (not notarized: macOS will ask you to approve the installer the first time)"
fi

log "Done: $OUT_DIR/$PKG_NAME"
