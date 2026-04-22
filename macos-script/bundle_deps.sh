#!/bin/bash
set -euo pipefail

APP_REL="${1:-stage/Spicy.app}"

APP="$(cd "$(dirname "$APP_REL")" && pwd)/$(basename "$APP_REL")"
BIN="$APP/Contents/MacOS/spicy.real"
FW="$APP/Contents/Frameworks"

[ -d "$APP" ] || { echo "ERROR: no app dir: $APP"; exit 1; }
[ -f "$BIN" ] || { echo "ERROR: no binary: $BIN"; exit 1; }

mkdir -p "$FW"

# какие пути считаем "внешними" и тащим внутрь
is_external() {
  case "$1" in
    /opt/homebrew/*|/usr/local/*) return 0 ;;
    *) return 1 ;;
  esac
}

base="$(basename "$1")"
dest_path() {
  echo "$FW/$(basename "$1")"
}

# Список обработанных абсолютных путей (чтобы не зациклиться)
SEEN="$FW/.bundled_seen.txt"
: > "$SEEN"

queue_file="$FW/.bundled_queue.txt"
: > "$queue_file"

enqueue() {
  local p="$1"
  grep -Fxq "$p" "$SEEN" 2>/dev/null && return 0
  echo "$p" >> "$queue_file"
  echo "$p" >> "$SEEN"
}

# стартуем с бинаря
enqueue "$BIN"

# получить зависимости через otool -L (только первые колонки, без "compatibility")
deps_of() {
  otool -L "$1" \
    | tail -n +2 \
    | awk '{print $1}'
}

# переписать ссылку в файле target: old -> @executable_path/../Frameworks/<basename>
rewrite_ref() {
  local target="$1"
  local old="$2"
  local bn
  bn="$(basename "$old")"
  local new="@executable_path/../Frameworks/$bn"
  install_name_tool -change "$old" "$new" "$target" 2>/dev/null || true
}

# выставить id у самой dylib внутри FW
set_id() {
  local lib="$1"
  local bn
  bn="$(basename "$lib")"
  install_name_tool -id "@executable_path/../Frameworks/$bn" "$lib" 2>/dev/null || true
}

# основной цикл
while read -r target; do
  [ -n "$target" ] || continue

  echo "== scan: $target =="

  while read -r dep; do
    [ -n "$dep" ] || continue

    # пропускаем системные, @rpath и уже относительные
    case "$dep" in
      @executable_path/*|@loader_path/*|@rpath/*) continue ;;
      /System/*|/usr/lib/*) continue ;;
    esac

    if is_external "$dep"; then
      dst="$(dest_path "$dep")"

      if [ ! -f "$dst" ]; then
        echo "  + copy: $dep -> $dst"
        ditto "$dep" "$dst"
        chmod 644 "$dst" || true
        set_id "$dst"
      fi

      echo "  ~ relink in target: $dep"
      rewrite_ref "$target" "$dep"

      # если target был бинарь или lib — добавим скопированную lib в очередь на дальнейший разбор
      enqueue "$dst"
    fi
  done < <(deps_of "$target")

done < "$queue_file"

# Вторая проходка: переписать зависимости ВНУТРИ самих dylib (между собой)
echo "== second pass: fix inter-lib refs =="
for lib in "$FW"/*.dylib; do
  [ -f "$lib" ] || continue
  while read -r dep; do
    [ -n "$dep" ] || continue
    if is_external "$dep"; then
      rewrite_ref "$lib" "$dep"
    fi
  done < <(deps_of "$lib")
  set_id "$lib"
done

# ad-hoc подпись всего app (после install_name_tool обязательно)
codesign --force --deep --sign - "$APP" || true

echo "DONE."
echo "Bundled libs:"
ls -1 "$FW" | sed 's/^/  - /'

