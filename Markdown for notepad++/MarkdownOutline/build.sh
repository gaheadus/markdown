#!/bin/bash
# Builds the MarkdownOutline Notepad++ plugin.
#
# Run from the MSYS2 MINGW64 shell:
#   bash build.sh
#
# Produces: MarkdownOutline.dll (64-bit, for the x64 notepad++.exe)

set -e

cd "$(dirname "$0")"

# The root of the Notepad++ sources: it provides the plugin API headers
# (PluginInterface.h, Notepad_plus_msgs.h, Docking.h) and the Scintilla
# headers. It is not a parent of this directory, so it has to be given.
NPP_ROOT="${NPP_ROOT:-/d/ziliao/code/opensource/notepad-plus-plus}"
CXX="${CXX:-x86_64-w64-mingw32-g++}"

CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE \
  -static-libgcc -static-libstdc++ \
  -I${NPP_ROOT}/PowerEditor/src/MISC/PluginsManager \
  -I${NPP_ROOT}/PowerEditor/src/WinControls/DockingWnd \
  -I${NPP_ROOT}/scintilla/include"

# The winpthreads runtime is linked statically by naming the archive explicitly:
# the gcc driver would otherwise add "-lwinpthread" dynamically, which would make
# the plugin depend on libwinpthread-1.dll sitting next to notepad++.exe.
# The archive is looked up next to the compiler, so that the script works in
# every MSYS2 environment (mingw64, ucrt64, clang64, ...).
CXX_BIN_DIR="$(dirname "$(command -v "${CXX}")")"
CXX_LIB_DIR="$(dirname "${CXX_BIN_DIR}")/lib"

if [ ! -f "${CXX_LIB_DIR}/libwinpthread.a" ]; then
	echo "!!! libwinpthread.a not found in ${CXX_LIB_DIR}" >&2
	echo "!!! Install it with: pacman -S mingw-w64-x86_64-winpthreads" >&2
	exit 1
fi

LDFLAGS="-shared -static-libgcc -static-libstdc++ -lcomctl32 -lgdi32 \
  -Wl,--whole-archive ${CXX_LIB_DIR}/libwinpthread.a -Wl,--no-whole-archive"

rm -f MarkdownOutline.o OutlinePanel.o MarkdownParser.o MarkdownOutline.dll

echo ">>> compiling"
${CXX} ${CXXFLAGS} -c MarkdownOutline.cpp -o MarkdownOutline.o
${CXX} ${CXXFLAGS} -c OutlinePanel.cpp   -o OutlinePanel.o
${CXX} ${CXXFLAGS} -c MarkdownParser.cpp -o MarkdownParser.o

echo ">>> linking"
${CXX} MarkdownOutline.o OutlinePanel.o MarkdownParser.o ${LDFLAGS} -o MarkdownOutline.dll

echo ">>> done"
ls -l MarkdownOutline.dll
