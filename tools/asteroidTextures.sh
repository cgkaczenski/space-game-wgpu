#!/bin/sh
# Builds tools/asteroidTextures.cpp and runs it: the 4K rock material in
# resources/textures/ (gitignored -- too big to commit) becomes the small
# textures in resources/asteroid/ that the game loads and git keeps -- among
# them rock_packed.png, brightness, normal x, normal y and height in one
# texture for the asteroid shader (A3).
#
#   tools/asteroidTextures.sh [size] [normalStrength]
#
# size defaults to 512 and has to divide 4096. Run from the repo root.
set -e

SIZE=${1:-512}
STRENGTH=${2:-6}

mkdir -p build/tools resources/asteroid
c++ -std=c++17 -O2 -Ithirdparty/stb_image/include tools/asteroidTextures.cpp -o build/tools/asteroidTextures
build/tools/asteroidTextures resources/textures resources/asteroid "$SIZE" "$STRENGTH"
ls -l resources/asteroid
