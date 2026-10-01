#!/bin/bash
# Cross-build the lab DLL, injector and netlab for 32-bit Windows with mingw-w64 (Linux/WSL/CI).
#   apt install mingw-w64   # Debian/Ubuntu
set -e
cd "$(dirname "$0")/../.."
mkdir -p build-win32
CXX=i686-w64-mingw32-g++
FLAGS="-std=c++17 -O2 -Wall -static -static-libgcc -static-libstdc++ -Isrc"
$CXX $FLAGS -shared -o build-win32/osmp_lab.dll src/lab/LabMain.cpp src/lab/RawEngine.cpp src/game/Replication.cpp src/game/GhostLab.cpp src/net/Protocol.cpp src/net/UdpSocket.cpp src/net/Session.cpp src/net/Interpolation.cpp -lws2_32 -Wl,--subsystem,windows
$CXX $FLAGS -o build-win32/inject.exe src/lab/inject.cpp
$CXX $FLAGS -o build-win32/osmp_netlab.exe tools/netlab.cpp src/net/Protocol.cpp src/net/UdpSocket.cpp src/net/Session.cpp src/net/Interpolation.cpp -lws2_32
ls -la build-win32
