#!/usr/bin/env bash
# System build dependencies for the desktop (Linux GL) build.
#
# sokol links against the platform's audio/GL/windowing libraries, which can't be
# vendored under deps/ — they must come from the system package manager.
#
#   tools/deps.sh check     report missing dev packages; exit non-zero if any
#   tools/deps.sh install   install them via apt / dnf / pacman (uses sudo)
#
# macOS needs nothing beyond Xcode's command line tools (system frameworks), so
# both modes are a no-op there. Run from anywhere.
set -u

mode="${1:-check}"

if [ "$(uname -s)" != "Linux" ]; then
    exit 0
fi

# pkg-config modules sokol needs on Linux (see LDLIBS_PLATFORM in examples/Makefile).
MODULES=(alsa gl x11 xi xcursor xrandr)

detect_pm() {
    if command -v apt-get >/dev/null 2>&1; then echo apt
    elif command -v dnf >/dev/null 2>&1; then echo dnf
    elif command -v pacman >/dev/null 2>&1; then echo pacman
    else echo unknown
    fi
}

# package_for <package-manager> <module|pkg-config>
package_for() {
    case "$1:$2" in
        apt:pkg-config)    echo pkg-config ;;
        apt:alsa)          echo libasound2-dev ;;
        apt:gl)            echo libgl-dev ;;
        apt:x11)           echo libx11-dev ;;
        apt:xi)            echo libxi-dev ;;
        apt:xcursor)       echo libxcursor-dev ;;
        apt:xrandr)        echo libxrandr-dev ;;
        dnf:pkg-config)    echo pkgconf-pkg-config ;;
        dnf:alsa)          echo alsa-lib-devel ;;
        dnf:gl)            echo mesa-libGL-devel ;;
        dnf:x11)           echo libX11-devel ;;
        dnf:xi)            echo libXi-devel ;;
        dnf:xcursor)       echo libXcursor-devel ;;
        dnf:xrandr)        echo libXrandr-devel ;;
        pacman:pkg-config) echo pkgconf ;;
        pacman:alsa)       echo alsa-lib ;;
        pacman:gl)         echo libglvnd ;;
        pacman:x11)        echo libx11 ;;
        pacman:xi)         echo libxi ;;
        pacman:xcursor)    echo libxcursor ;;
        pacman:xrandr)     echo libxrandr ;;
        *)                 echo "<$2 dev package>" ;;
    esac
}

pm="$(detect_pm)"

# Collect missing modules (pkg-config itself counts as one).
missing=()
if ! command -v pkg-config >/dev/null 2>&1; then
    missing=(pkg-config "${MODULES[@]}")
else
    for m in "${MODULES[@]}"; do
        pkg-config --exists "$m" || missing+=("$m")
    done
fi

packages=()
for m in "${missing[@]}"; do
    packages+=("$(package_for "$pm" "$m")")
done

case "$mode" in
    check)
        [ ${#missing[@]} -eq 0 ] && exit 0
        echo "libwgrender: missing system build dependencies: ${missing[*]}" >&2
        case "$pm" in
            apt)    echo "  install: sudo apt install ${packages[*]}" >&2 ;;
            dnf)    echo "  install: sudo dnf install ${packages[*]}" >&2 ;;
            pacman) echo "  install: sudo pacman -S --needed ${packages[*]}" >&2 ;;
            *)      echo "  install the development packages for: ${missing[*]}" >&2 ;;
        esac
        echo "  or run: make deps" >&2
        exit 1
        ;;
    install)
        if [ ${#missing[@]} -eq 0 ]; then
            echo "libwgrender: all system build dependencies present"
            exit 0
        fi
        echo "libwgrender: installing ${packages[*]}"
        case "$pm" in
            apt)    sudo apt-get install -y "${packages[@]}" ;;
            dnf)    sudo dnf install -y "${packages[@]}" ;;
            pacman) sudo pacman -S --needed --noconfirm "${packages[@]}" ;;
            *)      echo "libwgrender: unsupported package manager; install dev packages for: ${missing[*]}" >&2
                    exit 1 ;;
        esac
        ;;
    *)
        echo "usage: $0 [check|install]" >&2
        exit 2
        ;;
esac
