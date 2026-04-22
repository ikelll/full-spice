#!/usr/bin/env bash
set -euo pipefail

file="${1:-}"

if [[ -z "$file" ]]; then
    echo "Usage: $0 /path/to/file"
    exit 1
fi

if [[ ! -f "$file" ]]; then
    echo "Error: file '$file' not found"
    exit 1
fi

sudo cp -a "$file" "/Applications/GorizontVS-VDI.app/Contents/Frameworks/"
cp -a "$file" "/Users/ruslan/developer/client/collected-spicy/source-spice/stage/component/payload/GorizontVS-VDI.app/Contents/Frameworks/"
cp -a $file /Users/ruslan/developer/client/collected-spicy/source-spice/stage/GorizontVS-VDI.app/Contents/Frameworks
 
