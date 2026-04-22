#!/bin/bash
set -euo pipefail

APP_REL="${1:-stage/Spicy.app}"

# Абсолютный путь к .app
APP="$(cd "$(dirname "$APP_REL")" && pwd)/$(basename "$APP_REL")"
BIN="$APP/Contents/MacOS/spicy.real"
FW="$APP/Contents/Frameworks"
SRC="/opt/homebrew/lib/libgstreamer-1.0.0.dylib"
DST="$FW/libgstreamer-1.0.0.dylib"

echo "APP=$APP"
echo "BIN=$BIN"
echo "FW =$FW"

# проверки
[ -d "$APP" ] || { echo "ERROR: no app dir: $APP"; exit 1; }
[ -f "$BIN" ] || { echo "ERROR: no binary: $BIN"; exit 1; }
[ -f "$SRC" ] || { echo "ERROR: no source dylib: $SRC"; exit 1; }

mkdir -p "$FW"

# копируем (ditto надёжнее на macOS)
ditto "$SRC" "$DST"

# убеждаемся что файл реально появился
[ -f "$DST" ] || { echo "ERROR: copy failed, no file: $DST"; ls -la "$FW"; exit 1; }

# переписываем ссылку в бинаре (именно тот путь, который у тебя был в ошибке dyld)
install_name_tool -change \
  /opt/homebrew/opt/gstreamer/lib/libgstreamer-1.0.0.dylib \
  @executable_path/../Frameworks/libgstreamer-1.0.0.dylib \
  "$BIN" || true

# выставляем id у самой dylib
install_name_tool -id \
  @executable_path/../Frameworks/libgstreamer-1.0.0.dylib \
  "$DST"

echo "---- otool BIN ----"
otool -L "$BIN" | grep -E "gstreamer|/opt/homebrew" || true

echo "---- otool DYLIB ----"
otool -D "$DST" || true

# ad-hoc подпись (после install_name_tool почти всегда надо)
codesign --force --deep --sign - "$APP" || true

echo "OK"

