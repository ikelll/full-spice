#!/bin/bash
set -euo pipefail

# ---- Config ----
APP_NAME="Spicy"
APP_ID="ru.gorizontvs.spicy"
VERSION="${VERSION:-0.42.2-d33c-dirty}"

SPICE_PREFIX="${SPICE_PREFIX:-/opt/spice}"
SPICY_BIN="${SPICY_BIN:-$SPICE_PREFIX/bin/spicy}"

BREW_PREFIX="${BREW_PREFIX:-/opt/homebrew}"

OUT_DIR="${OUT_DIR:-$PWD/out}"
STAGE="${STAGE:-$PWD/stage}"

APP="${STAGE}/${APP_NAME}.app"
MACOS_DIR="${APP}/Contents/MacOS"
RES_DIR="${APP}/Contents/Resources"
FW_DIR="${APP}/Contents/Frameworks"

APP_SPICE="${RES_DIR}/spice"
APP_BREW="${RES_DIR}/brew"

APP_BIN="${APP_BREW}/bin"
APP_GI_TYPELIBS="${APP_BREW}/lib/girepository-1.0"
APP_PLUGINS_GST="${APP_BREW}/lib/gstreamer-1.0"
APP_GDK_PIXBUF="${APP_BREW}/lib/gdk-pixbuf-2.0"
APP_GST_SCANNER="${APP_BREW}/libexec/gstreamer-1.0/gst-plugin-scanner"

MARK_DIR="${STAGE}/.copied-marks"

die() { echo "ERROR: $*" >&2; exit 1; }

[ -x "${SPICY_BIN}" ] || die "SPICY_BIN not executable: ${SPICY_BIN}"
[ -d "${SPICE_PREFIX}" ] || die "SPICE_PREFIX not found: ${SPICE_PREFIX}"
[ -d "${BREW_PREFIX}" ] || die "BREW_PREFIX not found: ${BREW_PREFIX}"

mkdir -p "${OUT_DIR}" "${STAGE}" "${MARK_DIR}"
rm -rf "${APP}"
mkdir -p "${MACOS_DIR}" "${RES_DIR}" "${FW_DIR}" "${APP_SPICE}" "${APP_BREW}"

# ---- Utils ----
safe_mark_name() { echo "$1" | sed 's#[/ :()]#_#g' | sed 's#_#__#g'; }
already_copied() { [ -f "${MARK_DIR}/$(safe_mark_name "$1")" ]; }
mark_copied() { : > "${MARK_DIR}/$(safe_mark_name "$1")"; }

copy_file() {
  local src="$1" dst="$2"
  mkdir -p "$(dirname "$dst")"
  # always dereference symlinks for binaries/libs
  if [ -L "$src" ]; then
    cp -aL "$src" "$dst"
  else
    cp -a "$src" "$dst"
  fi
}

copy_into_frameworks() {
  local src="$1"
  local name dst
  name="$(basename "$src")"
  dst="${FW_DIR}/${name}"
  if already_copied "$dst"; then
    return 0
  fi
  copy_file "$src" "$dst"
  chmod u+w "$dst" 2>/dev/null || true
  mark_copied "$dst"
}

list_deps() {
  local bin="$1"
  otool -L "$bin" 2>/dev/null | tail -n +2 | awk '{print $1}' | sed 's/://'
}

# Resolve @rpath/foo.dylib -> real path we can copy
resolve_dep_path() {
  local dep="$1"

  # absolute deps
  if [[ "$dep" == /* ]]; then
    [ -e "$dep" ] && echo "$dep" && return 0
    return 1
  fi

  # @rpath deps: try SPICE then BREW (and opt/* symlinks)
  if [[ "$dep" == @rpath/* ]]; then
    local name="${dep#@rpath/}"
    local c

    c="${SPICE_PREFIX}/lib/${name}"
    [ -e "$c" ] && echo "$c" && return 0

    c="${BREW_PREFIX}/lib/${name}"
    [ -e "$c" ] && echo "$c" && return 0

    # Some brew libs live under opt/*/lib/
    c="$(find "${BREW_PREFIX}/opt" -maxdepth 4 -type f -name "${name}" 2>/dev/null | head -n1 || true)"
    [ -n "$c" ] && [ -e "$c" ] && echo "$c" && return 0

    return 1
  fi

  # other special tokens ignored
  return 1
}

add_rpath() {
  local bin="$1" rpath="$2"
  if otool -l "$bin" 2>/dev/null | grep -A2 LC_RPATH | grep -q "path $rpath"; then
    return 0
  fi
  install_name_tool -add_rpath "$rpath" "$bin" 2>/dev/null || true
}

set_id_rpath() {
  local dylib="$1"
  local name
  name="$(basename "$dylib")"
  install_name_tool -id "@rpath/${name}" "$dylib" 2>/dev/null || true
}

rewrite_dep() {
  local bin="$1" old="$2"

  # If dep is absolute in brew/spice -> rewrite to @rpath/name
  if [[ "$old" == "${SPICE_PREFIX}/"* || "$old" == "${BREW_PREFIX}/"* ]]; then
    local name
    name="$(basename "$old")"
    install_name_tool -change "$old" "@rpath/${name}" "$bin" 2>/dev/null || true
    return 0
  fi

  # If dep is already @rpath/foo.dylib -> keep as is
  return 0
}

collect_and_copy_deps() {
  local root="$1"
  local qfile="${STAGE}/.queue.$$"
  local sfile="${STAGE}/.seen.$$"
  : > "$qfile"
  : > "$sfile"
  echo "$root" >> "$qfile"

  while IFS= read -r cur; do
    [ -n "$cur" ] || continue
    if grep -qxF "$cur" "$sfile" 2>/dev/null; then
      continue
    fi
    echo "$cur" >> "$sfile"
    [ -e "$cur" ] || continue

    # Copy current object itself if it lives in brew/spice (or was resolved)
    if [[ "$cur" == "${SPICE_PREFIX}/"* || "$cur" == "${BREW_PREFIX}/"* ]]; then
      copy_into_frameworks "$cur"
    fi

    # Enqueue its deps
    list_deps "$cur" | while IFS= read -r dep; do
      [ -n "$dep" ] || continue
      local real
      if real="$(resolve_dep_path "$dep")"; then
        echo "$real" >> "$qfile"
      else
        # If dep is absolute brew/spice but missing, still ignore here
        true
      fi
    done
  done < "$qfile"

  rm -f "$qfile" "$sfile"
}

fixup_binary() {
  local bin="$1"
  chmod u+w "$bin" 2>/dev/null || true
  add_rpath "$bin" "@executable_path/../Frameworks"
  add_rpath "$bin" "@loader_path/../Frameworks"

  list_deps "$bin" | while IFS= read -r dep; do
    [ -n "$dep" ] || continue
    rewrite_dep "$bin" "$dep"
  done
}

fixup_all_frameworks() {
  for f in "${FW_DIR}"/*; do
    [ -f "$f" ] || continue
    if file "$f" | grep -q "dynamically linked shared library"; then
      set_id_rpath "$f"
    fi
    fixup_binary "$f"
  done
}

copy_tree_follow() {
  local src="$1" dst="$2"
  [ -e "$src" ] || return 0
  mkdir -p "$(dirname "$dst")"
  # -L: follow symlinks (brew heavily uses them)
  rsync -aL "$src" "$dst"
}

# ---- Copy /opt/spice payload ----
echo "[*] Copying ${SPICE_PREFIX} -> ${APP_SPICE}"
rsync -aL --delete "${SPICE_PREFIX}/" "${APP_SPICE}/"

# ---- Copy spicy binary ----
echo "[*] Installing spicy payload"
cp -a "${SPICY_BIN}" "${MACOS_DIR}/spicy.real"

# ---- Launcher ----
cat > "${MACOS_DIR}/spicy" <<'SH'
#!/bin/bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$(cd "$HERE/.." && pwd)"
FW="$APP/Frameworks"
RES="$APP/Resources"
SPICE="$RES/spice"
BREW="$RES/brew"

export PATH="$BREW/bin:$SPICE/bin:/usr/bin:/bin"
export DYLD_LIBRARY_PATH="$FW"

export GI_TYPELIB_PATH="$BREW/lib/girepository-1.0:$SPICE/lib/girepository-1.0"
export XDG_DATA_DIRS="$BREW/share:$SPICE/share:/usr/share"

export GST_PLUGIN_SYSTEM_PATH="$BREW/lib/gstreamer-1.0"
export GST_PLUGIN_PATH="$BREW/lib/gstreamer-1.0"
export GST_PLUGIN_SCANNER="$BREW/libexec/gstreamer-1.0/gst-plugin-scanner"
export GST_REGISTRY="${HOME}/Library/Caches/spicy-gst-registry.bin"

export GDK_PIXBUF_MODULEDIR="$BREW/lib/gdk-pixbuf-2.0/2.10.0/loaders"
export GDK_PIXBUF_MODULE_FILE="$BREW/lib/gdk-pixbuf-2.0/loaders.cache"

exec "$HERE/spicy.real" "$@"
SH
chmod +x "${MACOS_DIR}/spicy" "${MACOS_DIR}/spicy.real"

# ---- Info.plist ----
cat > "${APP}/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>${APP_NAME}</string>
  <key>CFBundleDisplayName</key><string>${APP_NAME}</string>
  <key>CFBundleIdentifier</key><string>${APP_ID}</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>spicy</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# ---- Copy runtime from Homebrew (FOLLOW SYMLINKS) ----
echo "[*] Copying runtime parts from Homebrew"
copy_tree_follow "${BREW_PREFIX}/opt/gdk-pixbuf/bin/gdk-pixbuf-query-loaders" "${APP_BIN}/gdk-pixbuf-query-loaders"
copy_tree_follow "${BREW_PREFIX}/opt/gstreamer/libexec/gstreamer-1.0/gst-plugin-scanner" "${APP_GST_SCANNER}"

copy_tree_follow "${BREW_PREFIX}/lib/girepository-1.0/" "${APP_GI_TYPELIBS}/"
copy_tree_follow "${BREW_PREFIX}/lib/gstreamer-1.0/" "${APP_PLUGINS_GST}/"
copy_tree_follow "${BREW_PREFIX}/lib/gdk-pixbuf-2.0/" "${APP_GDK_PIXBUF}/"

copy_tree_follow "${BREW_PREFIX}/share/glib-2.0/" "${APP_BREW}/share/glib-2.0/"
copy_tree_follow "${BREW_PREFIX}/share/icons/" "${APP_BREW}/share/icons/"
copy_tree_follow "${BREW_PREFIX}/share/locale/" "${APP_BREW}/share/locale/"

# ---- Collect deps (THIS NOW RESOLVES @rpath/*) ----
echo "[*] Collecting dylib dependencies closure"
collect_and_copy_deps "${MACOS_DIR}/spicy.real"
collect_and_copy_deps "${APP_GST_SCANNER}"

# gdk-pixbuf loaders are modules: include them too
if [ -d "${APP_GDK_PIXBUF}/2.10.0/loaders" ]; then
  find "${APP_GDK_PIXBUF}/2.10.0/loaders" -type f \( -name "*.so" -o -name "*.dylib" \) 2>/dev/null | while IFS= read -r p; do
    collect_and_copy_deps "$p"
  done
fi

# gst plugins
if [ -d "${APP_PLUGINS_GST}" ]; then
  find "${APP_PLUGINS_GST}" -type f -name "*.dylib" 2>/dev/null | while IFS= read -r p; do
    collect_and_copy_deps "$p"
  done
fi

# ---- Fixup install_name + rpath ----
echo "[*] Fixing install_name + rpath"
fixup_all_frameworks
fixup_binary "${MACOS_DIR}/spicy.real"
fixup_binary "${APP_GST_SCANNER}"

# ---- Build gdk-pixbuf loaders.cache at bundle time (AFTER fixup) ----
echo "[*] Building gdk-pixbuf loaders.cache inside app (build-time)"
GDK_CACHE_FILE="${APP_GDK_PIXBUF}/loaders.cache"
mkdir -p "${APP_GDK_PIXBUF}"

DYLD_LIBRARY_PATH="${FW_DIR}" \
GDK_PIXBUF_MODULEDIR="${APP_GDK_PIXBUF}/2.10.0/loaders" \
"${APP_BIN}/gdk-pixbuf-query-loaders" > "${GDK_CACHE_FILE}" || true

# ---- Quarantine removal ----
if command -v xattr >/dev/null 2>&1; then
  xattr -dr com.apple.quarantine "${APP}" 2>/dev/null || true
fi

# ---- Sanity: ensure our spice dylibs are present in Frameworks ----
echo "[*] Sanity check: spice client dylibs in Frameworks"
ls -la "${FW_DIR}/libspice-client-gtk-3.0"* "${FW_DIR}/libspice-client-glib-2.0"* 2>/dev/null || true

echo "[*] Sanity check for leaked brew/spice paths in spicy.real"
otool -L "${MACOS_DIR}/spicy.real" | grep -E "${BREW_PREFIX}|${SPICE_PREFIX}" || true

# ---- Create DMG ----
echo "[*] Creating DMG"
DMG_STAGE="${STAGE}/dmg-root"
rm -rf "${DMG_STAGE}"
mkdir -p "${DMG_STAGE}"
cp -a "${APP}" "${DMG_STAGE}/"
ln -sf /Applications "${DMG_STAGE}/Applications"

DMG_OUT="${OUT_DIR}/${APP_NAME}-${VERSION}.dmg"
hdiutil create -volname "${APP_NAME}" -srcfolder "${DMG_STAGE}" -ov -format UDZO "${DMG_OUT}"

echo "[*] DONE: ${DMG_OUT}"
