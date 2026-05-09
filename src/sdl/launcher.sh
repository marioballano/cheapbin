#!/bin/sh
# gxmenu launcher for cheapbin on the GKD bubble.
#
# Place this file alongside the cheapbin-aarch64 binary and the .gxmenu
# entry. Override DATA_DIR to pick a different default file to play.
#
# Argument 1, when given by the menu, is treated as the binary to play.

DIR=$(dirname "$0")
cd "$DIR" || exit 1

# Make any bundled .so libraries discoverable without a system install.
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"

# Default sample to feed when launched with no argument.
DEFAULT_FILE="$DIR/cheapbin-aarch64"

if [ $# -ge 1 ] && [ -f "$1" ]; then
    exec ./cheapbin-aarch64 "$@"
fi

exec ./cheapbin-aarch64 "$DEFAULT_FILE"
