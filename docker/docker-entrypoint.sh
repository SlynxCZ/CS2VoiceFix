#!/bin/bash
set -e

# Ensure xmake is in PATH
export PATH="/root/.local/bin:${PATH}"
export XMAKE_ROOT=y

# Configure & build native plugin
xmake f -y -p linux -a x86_64 --mode=release
xmake -y

# Prepare folders
mkdir -p build/package/addons/metamod
mkdir -p build/package/addons/CS2VoiceFix
mkdir -p build/package/addons/CS2VoiceFix/bin/linuxsteamrt64

# Copy configs
cp package/CS2VoiceFix.vdf build/package/addons/metamod
cp build/linux/x86_64/release/libCS2VoiceFix.so \
   build/package/addons/CS2VoiceFix/bin/linuxsteamrt64/CS2VoiceFix.so
