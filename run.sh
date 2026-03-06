#!/usr/bin/env bash

set -e

REQUIRED_PACKAGES=(
    build-essential
    libsdl2-dev
    libsdl2-ttf-dev
    libsdl2-image-dev
    libcurl4-openssl-dev
    libavformat-dev 
    libavcodec-dev
    libavutil-dev
    libswscale-dev
)

echo "checking dependencies..."

MISSING=()

for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ! dpkg -s "$pkg" &> /dev/null; then
        MISSING+=("$pkg")
    fi
done

if [ ${#MISSING[@]} -ne 0 ]; then
    echo "installing missing packages: ${MISSING[*]}"
    sudo apt update
    sudo apt install -y "${MISSING[@]}"
else
    echo "all required packages are installed"
fi

ROOT_DIR="$(pwd)"
PLUGINS_DIR="$ROOT_DIR/plugins"

echo "attempting to compile plugins..."

# Loop through each directory inside plugins
for plugin_dir in "$PLUGINS_DIR"/*/; do
    [ -d "$plugin_dir" ] || continue

    for cfile in "$plugin_dir"*.c; do
        [ -f "$cfile" ] || continue

        filename=$(basename -- "$cfile")
        name="${filename%.c}"

        echo "Compiling $cfile -> $plugin_dir$name.so"

        gcc -fPIC -shared \
            -o "$plugin_dir$name.so" \
            "$cfile" \
            `sdl2-config --cflags --libs` \
            -lSDL2_ttf -lSDL2_image -lm
    done
done

echo "compiling main and media files..."

gcc -o anny_board.out main.c media.c \
    `sdl2-config --cflags --libs` \
    -lSDL2_ttf -lSDL2_image \
    -lcurl -ldl -lm \
    -lavformat -lavcodec -lavutil -lswscale -lswresample

echo "running board..."

./anny_board.out
