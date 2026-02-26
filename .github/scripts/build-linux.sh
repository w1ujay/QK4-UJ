#!/bin/bash
set -ex

# =============================================================================
# QK4 Linux x86_64 Build Script
# =============================================================================
# This script builds QK4 natively for x86_64 Linux inside a Debian Trixie
# container and creates a portable tarball with all required libraries bundled.
#
# Target: x86_64 Linux running Debian Trixie / Ubuntu 24.10+ or compatible
# Qt Version: 6.8+ (from Debian Trixie repos)
# =============================================================================

LIB_DIR=/usr/lib/x86_64-linux-gnu
QT_PLUGINS=$LIB_DIR/qt6/plugins

apt-get update

# Build tools
apt-get install -y cmake g++ file patchelf pkg-config ccache

# Qt6 development libraries
apt-get install -y \
  qt6-base-dev qt6-base-private-dev \
  qt6-multimedia-dev qt6-shadertools-dev \
  qt6-serialport-dev qt6-svg-dev

# System libraries
apt-get install -y \
  libopus-dev libhidapi-dev libssl-dev \
  libasound2-dev libpulse-dev

# Configure ccache
export CCACHE_DIR="${CCACHE_DIR:-/tmp/ccache}"
export CCACHE_MAXSIZE=500M
ccache --zero-stats

# Version passed as first argument (from release workflow tag)
VERSION_FLAG=""
if [ -n "$1" ]; then
  VERSION_FLAG="-DQK4_VERSION_FULL=$1"
  echo "Building version: $1"
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  $VERSION_FLAG
cmake --build build -j$(nproc)

echo "=== ccache stats ==="
ccache --show-stats

# Verify output is x86_64
file build/QK4 | grep -q "x86-64" || { echo "ERROR: Binary is not x86_64!"; exit 1; }

# =============================================================================
# Create portable bundle
# =============================================================================
DIST=dist/QK4
mkdir -p $DIST/lib
mkdir -p $DIST/plugins

cp build/QK4 $DIST/
cp resources/99-kpod.rules $DIST/

# Helper: copy a library and fail the build if it's missing
require_lib() {
  local src="$1" dst="$2"
  cp -rL "$src" "$dst" || { echo "ERROR: Required library missing: $src"; exit 1; }
}

# Helper: copy a library/directory if it exists (truly optional)
optional_lib() {
  local src="$1" dst="$2"
  if ls $src 1>/dev/null 2>&1; then
    cp -rL $src "$dst"
  else
    echo "NOTE: Optional library not found: $src (skipping)"
  fi
}

# -----------------------------------------------------------------------------
# Qt Libraries (required)
# -----------------------------------------------------------------------------
for lib in \
  libQt6Core.so.6 \
  libQt6Gui.so.6 \
  libQt6Widgets.so.6 \
  libQt6Network.so.6 \
  libQt6Multimedia.so.6 \
  libQt6SerialPort.so.6 \
  libQt6ShaderTools.so.6 \
  libQt6OpenGL.so.6 \
  libQt6OpenGLWidgets.so.6 \
  libQt6DBus.so.6 \
  libQt6Svg.so.6 \
  libQt6XcbQpa.so.6; do
  require_lib "$LIB_DIR/$lib" "$DIST/lib/"
done

# -----------------------------------------------------------------------------
# Qt Plugins
# -----------------------------------------------------------------------------
cp -r $QT_PLUGINS/platforms $DIST/plugins/
optional_lib "$QT_PLUGINS/xcbglintegrations" "$DIST/plugins/"
cp -r $QT_PLUGINS/tls $DIST/plugins/
optional_lib "$QT_PLUGINS/imageformats" "$DIST/plugins/"
optional_lib "$QT_PLUGINS/iconengines" "$DIST/plugins/"

# -----------------------------------------------------------------------------
# System Libraries (required)
# -----------------------------------------------------------------------------
for lib in \
  libopus.so.0 \
  libhidapi-hidraw.so.0 \
  libhidapi-libusb.so.0 \
  libssl.so.3 \
  libcrypto.so.3; do
  require_lib "$LIB_DIR/$lib" "$DIST/lib/"
done

# PulseAudio/ALSA libraries (required for audio)
for lib in \
  libpulse.so.0 \
  libpulse-simple.so.0 \
  libasound.so.2; do
  require_lib "$LIB_DIR/$lib" "$DIST/lib/"
done

# PulseAudio private library
PULSECOMMON=$(ls $LIB_DIR/pulseaudio/libpulsecommon-*.so 2>/dev/null) \
  || { echo "ERROR: libpulsecommon not found in $LIB_DIR/pulseaudio/"; exit 1; }
cp -L $PULSECOMMON $DIST/lib/

# -----------------------------------------------------------------------------
# Verify bundle completeness
# -----------------------------------------------------------------------------
echo "=== Verifying bundle ==="
MISSING=0
for check in \
  "$DIST/QK4" \
  "$DIST/lib/libQt6Core.so.6" \
  "$DIST/lib/libQt6Multimedia.so.6" \
  "$DIST/lib/libopus.so.0" \
  "$DIST/lib/libpulse.so.0" \
  "$DIST/lib/libasound.so.2" \
  "$DIST/lib/libssl.so.3" \
  "$DIST/plugins/platforms"; do
  if [ ! -e "$check" ]; then
    echo "ERROR: Missing from bundle: $check"
    MISSING=1
  fi
done

if ! ls $DIST/lib/libpulsecommon-*.so 1>/dev/null 2>&1; then
  echo "ERROR: Missing from bundle: libpulsecommon-*.so"
  MISSING=1
fi

if [ $MISSING -ne 0 ]; then
  echo "Bundle verification FAILED"
  exit 1
fi

echo "Bundle verification passed"
echo ""
echo "=== Bundle contents ==="
find $DIST -type f | sort
echo ""

# -----------------------------------------------------------------------------
# Set rpath so binary finds bundled libs
# -----------------------------------------------------------------------------
patchelf --set-rpath '$ORIGIN/lib' $DIST/QK4
find $DIST/lib -name "*.so*" -type f -exec patchelf --set-rpath '$ORIGIN' {} \; 2>/dev/null || true
find $DIST/plugins -name "*.so*" -type f -exec patchelf --set-rpath '$ORIGIN/../../lib' {} \; 2>/dev/null || true

# -----------------------------------------------------------------------------
# Create launcher script
# -----------------------------------------------------------------------------
cat > $DIST/run.sh << 'LAUNCHER'
#!/bin/bash
# QK4 Launcher for Linux x86_64
# This script sets up the environment to use bundled libraries

DIR="$(cd "$(dirname "$0")" && pwd)"

# Install KPOD udev rule if not already present (requires sudo, one-time setup)
UDEV_RULE="$DIR/99-kpod.rules"
UDEV_DEST="/etc/udev/rules.d/99-kpod.rules"
if [ -f "$UDEV_RULE" ] && [ ! -f "$UDEV_DEST" ]; then
    echo "KPOD udev rule not installed. Installing for USB HID access..."
    sudo cp "$UDEV_RULE" "$UDEV_DEST"
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    echo "KPOD udev rule installed. You may need to re-plug the KPOD device."
fi

# Use bundled libraries
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"

# Use bundled Qt plugins
export QT_PLUGIN_PATH="$DIR/plugins"

# Use XCB (X11) platform — we ship XCB plugins, not Wayland
export QT_QPA_PLATFORM=xcb

# Launch the application
exec "$DIR/QK4" "$@"
LAUNCHER
chmod +x $DIST/run.sh

# -----------------------------------------------------------------------------
# Package
# -----------------------------------------------------------------------------
cd dist
tar czf ../QK4-linux-x86_64.tar.gz QK4/

echo "============================================="
echo "Build complete!"
echo "Output: QK4-linux-x86_64.tar.gz"
echo "============================================="
