#!/bin/bash
set -e

echo "Installing aqtinstall..."
pip3 install --user aqtinstall

echo "Installing Qt 6.8.1 (this may take a few minutes)..."
aqt install-qt linux desktop 6.8.1 gcc_64 -m qtmultimedia qtserialport qtshadertools

echo ""
echo "Qt 6.8.1 installed to: $HOME/6.8.1/gcc_64"
echo ""
echo "To build QK4, run:"
echo "  rm -rf build"
echo "  cmake -B build -DCMAKE_PREFIX_PATH=\"$HOME/6.8.1/gcc_64\" -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build build -j\$(nproc)"
