#!/bin/bash

set -euo pipefail

sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get -qq update
sudo apt-get -y install libgtk2.0-dev freeglut3 freeglut3-dev libglew-dev mesa-common-dev build-essential libglm-dev libxxf86vm-dev libfreeimage-dev pandoc cmake p7zip-full ninja-build xvfb rpm g++-5

# Patch and build wxWidgets

wget https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.0/wxWidgets-3.1.0.7z
if [[ "daf03ed0006e41334f10ceeb3aa2d20c63aacd42" != $(sha1sum wxWidgets-3.1.0.7z | cut -f1 -d' ') ]] ; then exit 1 ; fi
7z x -o"wxWidgets" -y wxWidgets-3.1.0.7z > /dev/null
cd wxWidgets
patch -p0 < ../patches/wxWidgets/*.patch
mkdir build-release
cd build-release
CC=gcc-5 CXX=g++-5 ../configure --quiet --disable-shared --with-opengl --with-cxx=14 --with-gtk=2 --prefix="$(pwd)/install" --disable-precomp-headers --with-libpng=builtin --with-libtiff=builtin --with-libjpeg=builtin && make -j2 && make install
cd ..
cd ..

# Build TrenchBroom

mkdir build
cd build
CC=gcc-5 CXX=g++-5 cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-Werror -DwxWidgets_PREFIX="$(pwd)/../wxWidgets/build-release/install"
cmake --build . --config Release
cpack

./generate_checksum_deb.sh
./generate_checksum_rpm.sh

# Create AppImage for Linux

mkdir -p appdir/usr/bin appdir/usr/share/icons appdir/usr/lib
strip trenchbroom
cp trenchbroom appdir/usr/bin/trenchbroom
cp ../app/resources/linux/trenchbroom.desktop appdir/trenchbroom.desktop
cp ../app/resources/linux/icons/icon_256.png appdir/usr/share/icons/trenchbroom.png
# Copy libpangoft2 manually to avoid Freetype library errors
cp "/usr/lib/x86_64-linux-gnu/libpangoft2-1.0.so.0.3600.3" "appdir/usr/lib/libpangoft2-1.0.so.0"

wget -q "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage"
chmod +x "linuxdeployqt-continuous-x86_64.AppImage"
./linuxdeployqt-continuous-x86_64.AppImage --appimage-extract
rm -f "linuxdeployqt-continuous-x86_64.AppImage"
./squashfs-root/AppRun "appdir/trenchbroom.desktop" -appimage
# Move the AppImage to the build artifacts directory
cp *.AppImage ..

# Run tests (wxgtk needs an X server running for the app to initialize)

Xvfb :10 &
export DISPLAY=:10
./TrenchBroom-Test

echo "Shared libraries used:"
ldd --verbose ./trenchbroom

echo "Debian dependencies:"
./print_debian_dependencies.sh
