#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

default_build_dir="${repo_root}/build/Desktop_Qt_6_10_2-Debug"
default_icon_source="${repo_root}/src/HexEdit/resources/q22.png"

if [[ $# -gt 2 ]]; then
    echo "Usage: ${0##*/} [build-dir] [icon-file]" >&2
    echo "       ${0##*/} [icon-file]" >&2
    exit 2
fi

build_dir="${1:-${default_build_dir}}"
icon_source="${2:-${default_icon_source}}"

if [[ $# -eq 1 && -f "$1" ]]; then
    build_dir="${default_build_dir}"
    icon_source="$1"
fi

binary="${build_dir}/q22"

if [[ ! -x "${binary}" ]]; then
    echo "q22 binary not found or not executable: ${binary}" >&2
    echo "Build first, or pass the build directory as the first argument." >&2
    exit 1
fi

if [[ ! -f "${icon_source}" ]]; then
    echo "q22 icon not found: ${icon_source}" >&2
    exit 1
fi

applications_dir="${XDG_DATA_HOME:-${HOME}/.local/share}/applications"
icons_dir="${XDG_DATA_HOME:-${HOME}/.local/share}/icons/hicolor/256x256/apps"
desktop_file="${applications_dir}/q22.desktop"
icon_file="${icons_dir}/q22.png"
desktop_tmp=""
icon_tmp=""

cleanup() {
    [[ -n "${desktop_tmp}" && -e "${desktop_tmp}" ]] && rm -f "${desktop_tmp}"
    [[ -n "${icon_tmp}" && -e "${icon_tmp}" ]] && rm -f "${icon_tmp}"
    return 0
}
trap cleanup EXIT

mkdir -p "${applications_dir}" "${icons_dir}"

icon_tmp="$(mktemp "${icons_dir}/q22.png.XXXXXX")"
cp "${icon_source}" "${icon_tmp}"
chmod 0644 "${icon_tmp}"
mv -f "${icon_tmp}" "${icon_file}"
icon_tmp=""

desktop_tmp="$(mktemp "${applications_dir}/q22.desktop.XXXXXX")"
cat > "${desktop_tmp}" <<EOF
[Desktop Entry]
Type=Application
Name=q22
GenericName=Hex Editor
Comment=Catch22 hex editor
Exec=${binary} %F
Icon=q22
StartupWMClass=q22
Categories=Utility;Development;
Terminal=false
EOF

chmod 0644 "${desktop_tmp}" "${icon_file}"
mv -f "${desktop_tmp}" "${desktop_file}"
desktop_tmp=""

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${applications_dir}" >/dev/null 2>&1 || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -q "${XDG_DATA_HOME:-${HOME}/.local/share}/icons/hicolor" >/dev/null 2>&1 || true
fi

echo "Installed ${desktop_file}"
echo "Installed ${icon_file}"
