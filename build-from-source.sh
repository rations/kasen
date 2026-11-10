#!/bin/sh
# build-from-source.sh — build and install Kasen from source on Haiku.
#
# Build these from source FIRST, in order (each has its own build-from-source.sh):
#   1. jack-port-renku   (libjack)
#   2. VST3-renku        (SDK static libs at ~/VST3-renku/build/lib/Release)
# The Makefile's VST3_SDK / VST3_LIB default to ~/VST3-renku.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)

pkgman install -y gcc make pkgconfig haiku_devel glib2_devel libsndfile_devel libsamplerate_devel
export PKG_CONFIG_PATH="/boot/system/non-packaged/lib/pkgconfig:/boot/system/lib/pkgconfig:$PKG_CONFIG_PATH"

cd "$HERE"
make clean
make

mkdir -p /boot/system/non-packaged/apps
cp Kasen /boot/system/non-packaged/apps/Kasen
mkdir -p /boot/system/non-packaged/add-ons/Kasen
cp backends/jack.so /boot/system/non-packaged/add-ons/Kasen/jack.so
echo ">> Installed Kasen to /boot/system/non-packaged/apps/Kasen"
echo ">> and its JACK support to /boot/system/non-packaged/add-ons/Kasen/jack.so"
echo ">> Start jackd, then launch Kasen from Deskbar (Applications) or run ./Kasen"
