#!/usr/bin/env bash

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
    libxml2-dev
)

REQUIRED_PACKAGES_ARCH=(
    base-devel
    sdl3
    sdl2-compat
    sdl2_ttf
    sdl2_image
    curl
    ffmpeg
    libxml2
)

echo "checking package manager..."

which dpkg &> /dev/null
HAS_DPKG=$?
which pacman &> /dev/null
HAS_PACMAN=$?
MISSING=()

# errors are expected above (most people don't have both installed), so set -e must not appear above here

set -e

# theoretically this could be made into an array of various package manager names, then with another of their existance value...
# but that seems like unneccesary complication for what will probably just be these two


USED_PKGMNGR=0

if [ 0 -eq $HAS_PACMAN ]; then
    if [ 0 -eq $HAS_DPKG ]; then
        echo "both pacman and dpkg are installed. which would you like to use?"
        echo "1) pacman"
        echo "2) dpkg"
        echo
        # this should ~probably~ check if its an input that.. makes sense, probably fine for our purposes though..
        read USED_PKGMNGR
    else
        USED_PKGMNGR=1
    fi
else
    if [ 0 -eq $HAS_DPKG ]; then
        USED_PKGMNGR=2
    else
        echo "no compatible package manager installed :("
        echo "this will still try to compile, but be warned it may not work as expected..."
    fi
fi


echo "checking dependencies..."

if [ 1 -eq $USED_PKGMNGR ]; then
    for pkg in "${REQUIRED_PACKAGES_ARCH[@]}"; do
        if ! pacman -Q "$pkg" 2> /dev/null; then
            MISSING+=("$pkg")
        fi
    done

    if [ ${#MISSING[@]} -ne 0 ]; then
        echo "installing missing packages: ${MISSING[*]}"
        sudo pacman -Syu "${MISSING[@]}"
    else
        echo "all required packages are installed"
    fi
fi

if [ 2 -eq $USED_PKGMNGR ]; then
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

        gcc -g -fPIC -shared \
            -o "$plugin_dir$name.so" \
            "$cfile" \
            `sdl2-config --cflags --libs` \
            -lSDL2_ttf -lSDL2_image -lm \
            -lcurl
    done
done

echo "compiling main and media files..."

gcc -g -o anny_board.out main.c media.c \
    `sdl2-config --cflags --libs` \
    -lSDL2_ttf -lSDL2_image \
    -lcurl -ldl -lm \
    -lavformat -lavcodec -lavutil -lswscale -lswresample

echo "running board..."

./anny_board.out
