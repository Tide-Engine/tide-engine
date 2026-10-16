#!/bin/sh
# Splits every piece tools/pieces.txt lists into art/kit, and makes art/tex.
#   sh make_kit.sh <Medieval Village MegaKit dir> <Fantasy Props MegaKit dir> <Stylized Nature MegaKit dir> [blender]
# The dirs are the kits' Standard downloads, unzipped (https://quaternius.com).
set -e
here=$(cd "$(dirname "$0")" && pwd)
village=$1; props=$2; nature=$3
blender=${4:-"C:/Program Files/Blender Foundation/Blender 4.4/blender.exe"}
out="$here/.."
rm -f "$out"/*.glb "$out/parts.json"
files=$(grep -v '^#' "$here/pieces.txt" | while read kit piece; do
    case $kit in
        village) echo "$village/glTF/$piece.gltf" ;;
        props) echo "$props/Exports/glTF/$piece.gltf" ;;
        nature) echo "$nature/glTF/$piece.gltf" ;;
    esac
done)
"$blender" -b --factory-startup --python "$here/split_kit.py" -- "$out" $files
python -P "$here/make_textures.py" "$village" "$props" "$nature" "$out/../tex"
