#!/usr/bin/env bash

set -euo pipefail

deb_directory=${1:?Debian package directory is required}
artifact_directory=${2:?Artifact directory is required}
work_directory=${3:-$PWD/workdir/linux-bundles}
bundle_format=${4:-all}
app_id=io.github.lazy_jubei.LibreOfficeVSDX

case $bundle_format in
    all|appimage|flatpak|snap) ;;
    *)
        echo "Unsupported Linux bundle format: $bundle_format" >&2
        exit 1
        ;;
esac

bundle_artifacts=()

rm -rf "$work_directory"
mkdir -p "$work_directory/root" "$artifact_directory"

mapfile -d '' deb_packages < <(find "$deb_directory" -maxdepth 1 -type f -name '*.deb' -print0 | sort -z)
if ((${#deb_packages[@]} == 0)); then
    echo "No Debian packages found in $deb_directory" >&2
    exit 1
fi

for package in "${deb_packages[@]}"; do
    dpkg-deb --extract "$package" "$work_directory/root"
done

office_program=$(find "$work_directory/root" \( -type f -o -type l \) -path '*/program/soffice' -print -quit)
if [[ -z $office_program ]]; then
    echo 'Could not locate the LibreOffice program directory in the Debian packages.' >&2
    exit 1
fi

office_directory=${office_program%/program/soffice}
office_relative=${office_directory#"$work_directory/root"}
office_command=${office_relative#/}/program/soffice

home_directory="$work_directory/home"
mkdir -p "$home_directory"
HOME="$home_directory" "$office_program" --headless --version

portable_root="$work_directory/root"

dependency_list="$work_directory/dependencies.txt"
: > "$dependency_list"
while IFS= read -r -d '' candidate; do
    if file --brief "$candidate" | grep -q '^ELF'; then
        lddtree -l "$candidate" >> "$dependency_list" 2>/dev/null || true
    fi
done < <(find "$office_directory" -type f -print0)

while IFS= read -r dependency; do
    [[ $dependency == /* && -f $dependency ]] || continue
    [[ $dependency == "$work_directory/root"/* ]] && continue
    case $(basename "$dependency") in
        ld-linux*.so*|libc.so.*|libdl.so.*|libm.so.*|libpthread.so.*|libresolv.so.*|librt.so.*|libutil.so.*)
            continue
            ;;
    esac
    destination="$portable_root$dependency"
    mkdir -p "${destination%/*}"
    cp -L "$dependency" "$destination"
done < <(sort -u "$dependency_list")

icon=$(find "$work_directory/root" -type f -path '*/icons/hicolor/256x256/apps/*.png' -print -quit)
if [[ -z $icon ]]; then
    icon=$(find "$work_directory/root" -type f -name '*.png' -path '*/icons/hicolor/*/apps/*' -print -quit)
fi
if [[ -z $icon ]]; then
    echo 'Could not locate a LibreOffice application icon.' >&2
    exit 1
fi

if [[ $bundle_format == all || $bundle_format == appimage ]]; then
    appimage_directory="$work_directory/LibreOfficeDev-VSDX.AppDir"
    cp -al "$portable_root" "$appimage_directory"
    cat > "$appimage_directory/AppRun" <<EOF
#!/bin/sh
HERE=\$(dirname "\$(readlink -f "\$0")")
export LD_LIBRARY_PATH="\$HERE/lib/x86_64-linux-gnu:\$HERE/usr/lib/x86_64-linux-gnu:\$HERE$office_relative/program\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec "\$HERE/$office_command" "\$@"
EOF
    chmod +x "$appimage_directory/AppRun"
    cat > "$appimage_directory/libreoffice-vsdx.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=LibreOfficeDev VSDX
Comment=Office suite with VSDX export support
Exec=AppRun %U
Icon=libreoffice-vsdx
Terminal=false
Categories=Office;WordProcessor;Spreadsheet;Presentation;Graphics;
MimeType=application/vnd.visio;application/vnd.ms-visio.drawing;application/vnd.ms-visio.drawing.main+xml;
EOF
    cp "$icon" "$appimage_directory/libreoffice-vsdx.png"

    appimagetool="$work_directory/appimagetool-x86_64.AppImage"
    curl --fail --location --retry 3 \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage \
        --output "$appimagetool"
    echo "a6d71e2b6cd66f8e8d16c37ad164658985e0cf5fcaa950c90a482890cb9d13e0  $appimagetool" | sha256sum --check
    chmod +x "$appimagetool"
    appimage="$artifact_directory/LibreOfficeDev-VSDX-x86_64.AppImage"
    ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$appimagetool" "$appimage_directory" "$appimage"
    chmod +x "$appimage"
    HOME="$home_directory" APPIMAGE_EXTRACT_AND_RUN=1 "$appimage" --headless --version
    bundle_artifacts+=("$appimage")
fi

if [[ $bundle_format == all || $bundle_format == flatpak ]]; then
    flatpak_build="$work_directory/flatpak-build"
    flatpak_repository="$work_directory/flatpak-repository"
    mkdir -p "$flatpak_build/files/bin" "$flatpak_build/files/share/applications" \
        "$flatpak_build/files/share/icons/hicolor/256x256/apps" "$flatpak_build/files/share/metainfo" \
        "$flatpak_build/export/share/applications" "$flatpak_build/export/share/icons/hicolor/256x256/apps" \
        "$flatpak_build/export/share/metainfo"
    cp -al "$portable_root/." "$flatpak_build/files/"
    cat > "$flatpak_build/files/bin/libreoffice-vsdx" <<EOF
#!/bin/sh
export LD_LIBRARY_PATH="/app/lib/x86_64-linux-gnu:/app/usr/lib/x86_64-linux-gnu:/app$office_relative/program\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec "/app/$office_command" "\$@"
EOF
    chmod +x "$flatpak_build/files/bin/libreoffice-vsdx"
    cat > "$flatpak_build/files/share/applications/$app_id.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LibreOfficeDev VSDX
Comment=Office suite with VSDX export support
Exec=libreoffice-vsdx %U
Icon=$app_id
Terminal=false
Categories=Office;WordProcessor;Spreadsheet;Presentation;Graphics;
MimeType=application/vnd.visio;application/vnd.ms-visio.drawing;application/vnd.ms-visio.drawing.main+xml;
EOF
    cp "$icon" "$flatpak_build/files/share/icons/hicolor/256x256/apps/$app_id.png"
    cat > "$flatpak_build/files/share/metainfo/$app_id.metainfo.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>$app_id</id>
  <name>LibreOfficeDev VSDX</name>
  <summary>Office suite with VSDX export support</summary>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>MPL-2.0</project_license>
  <launchable type="desktop-id">$app_id.desktop</launchable>
  <description><p>Development build of LibreOffice with VSDX export support.</p></description>
</component>
EOF
    desktop-file-validate "$flatpak_build/files/share/applications/$app_id.desktop"
    appstream-util validate-relax "$flatpak_build/files/share/metainfo/$app_id.metainfo.xml"
    cp "$flatpak_build/files/share/applications/$app_id.desktop" "$flatpak_build/export/share/applications/"
    cp "$flatpak_build/files/share/icons/hicolor/256x256/apps/$app_id.png" \
        "$flatpak_build/export/share/icons/hicolor/256x256/apps/"
    cp "$flatpak_build/files/share/metainfo/$app_id.metainfo.xml" "$flatpak_build/export/share/metainfo/"
    cat > "$flatpak_build/metadata" <<EOF
[Application]
name=$app_id
runtime=org.freedesktop.Platform/x86_64/25.08
sdk=org.freedesktop.Sdk/x86_64/25.08
command=libreoffice-vsdx

[Context]
shared=network;ipc;
sockets=fallback-x11;wayland;pulseaudio;cups;
devices=dri;
filesystems=host;xdg-config/gtk-3.0:ro;xdg-config/fontconfig:ro;xdg-run/gvfsd;

[Session Bus Policy]
org.libreoffice.LibreOfficeIpc0=own
org.gtk.vfs.*=talk

[Environment]
LIBO_FLATPAK=1
EOF
    flatpak build-export --arch=x86_64 "$flatpak_repository" "$flatpak_build" main
    flatpak_bundle="$artifact_directory/LibreOfficeDev-VSDX-x86_64.flatpak"
    flatpak build-bundle --arch=x86_64 \
        --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
        "$flatpak_repository" "$flatpak_bundle" "$app_id" main
    flatpak build-import-bundle "$work_directory/flatpak-validation-repository" "$flatpak_bundle"
    bundle_artifacts+=("$flatpak_bundle")
fi

if [[ $bundle_format == all || $bundle_format == snap ]]; then
    snap_root="$work_directory/snap-root"
    cp -al "$portable_root" "$snap_root"
    mkdir -p "$snap_root/bin" "$snap_root/meta/gui"
    cat > "$snap_root/bin/libreoffice-vsdx" <<EOF
#!/bin/sh
export LD_LIBRARY_PATH="\$SNAP/lib/x86_64-linux-gnu:\$SNAP/usr/lib/x86_64-linux-gnu:\$SNAP$office_relative/program\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec "\$SNAP/$office_command" "\$@"
EOF
    chmod +x "$snap_root/bin/libreoffice-vsdx"
    cp "$icon" "$snap_root/meta/gui/icon.png"
    cat > "$snap_root/meta/gui/libreoffice-vsdx.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=LibreOfficeDev VSDX
Comment=Office suite with VSDX export support
Exec=libreoffice-vsdx.libreoffice %U
Icon=${SNAP}/meta/gui/icon.png
Terminal=false
Categories=Office;WordProcessor;Spreadsheet;Presentation;Graphics;
EOF
    version="0+git.${GITHUB_SHA:-local}"
    version=${version:0:20}
    cat > "$snap_root/meta/snap.yaml" <<EOF
name: libreoffice-vsdx
version: '$version'
summary: LibreOffice development build with VSDX export support
description: LibreOffice development build with VSDX export support.
grade: devel
confinement: strict
base: core24
architectures:
  - amd64
apps:
  libreoffice:
    command: bin/libreoffice-vsdx
    desktop: meta/gui/libreoffice-vsdx.desktop
    environment:
      XDG_DATA_DIRS: \$SNAP/usr/share
    plugs:
      - audio-playback
      - cups
      - desktop
      - desktop-legacy
      - gsettings
      - home
      - network
      - network-bind
      - opengl
      - removable-media
      - wayland
      - x11
EOF
    SNAP="$snap_root" HOME="$home_directory" "$snap_root/bin/libreoffice-vsdx" --headless --version
    snap_bundle="$artifact_directory/libreoffice-vsdx_${version}_amd64.snap"
    snap pack --filename "$snap_bundle" "$snap_root"
    unsquashfs -cat "$snap_bundle" meta/snap.yaml
    bundle_artifacts+=("$snap_bundle")
fi

sha256sum "${bundle_artifacts[@]}"