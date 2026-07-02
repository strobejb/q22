#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

build_dir="${1:-${repo_root}/build/Desktop_Qt_6_10_2-Debug}"
binary="${build_dir}/q22"
icon_source="${repo_root}/src/HexEdit/resources/q22.png"

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

mkdir -p "${applications_dir}" "${icons_dir}"
cp "${icon_source}" "${icon_file}"

cat > "${desktop_file}" <<EOF
[Desktop Entry]
Type=Application
Name=q22
GenericName=Hex Editor
Comment=Catch22 hex editor
Exec=${binary} %F
Icon=${icon_file}
StartupWMClass=q22
Categories=Utility;Development;
Terminal=false
EOF

chmod 0644 "${desktop_file}" "${icon_file}"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${applications_dir}" >/dev/null 2>&1 || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -q "${XDG_DATA_HOME:-${HOME}/.local/share}/icons/hicolor" >/dev/null 2>&1 || true
fi

echo "Installed ${desktop_file}"
echo "Installed ${icon_file}"
