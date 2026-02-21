#!/bin/bash
# QK4 launcher script with Qt 6.8.1 library paths

export LD_LIBRARY_PATH="$HOME/6.8.1/gcc_64/lib:$LD_LIBRARY_PATH"
export QT_QPA_PLATFORM=xcb

cd "$(dirname "$0")"
./build/QK4 "$@"
