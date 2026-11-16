# Makefile for the MarkdownOutline Notepad++ plugin
#
# Requires MinGW-w64 (MSYS2 UCRT64 or MINGW64 environment):
#   pacman -S --needed mingw-w64-x86_64-toolchain
#
# Usage:  make -f Makefile
# Output: MarkdownOutline.dll (64-bit)

CXX      ?= x86_64-w64-mingw32-g++
WINDRES  ?= x86_64-w64-mingw32-windres

# The root of the Notepad++ sources: it provides the plugin API headers
# (PluginInterface.h, Notepad_plus_msgs.h, Docking.h) and the Scintilla
# headers. It is not a parent of this directory, so it has to be given.
NPP_ROOT ?= /d/ziliao/code/opensource/notepad-plus-plus

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -municode -static-libgcc -static-libstdc++ \
            -I$(NPP_ROOT)/PowerEditor/src/MISC/PluginsManager \
            -I$(NPP_ROOT)/PowerEditor/src/WinControls/DockingWnd \
            -I$(NPP_ROOT)/scintilla/include

# The winpthreads runtime is linked statically by naming the archive explicitly:
# the gcc driver would otherwise add "-lwinpthread" dynamically, which would make
# the plugin depend on libwinpthread-1.dll sitting next to notepad++.exe.
# The archive is looked up next to the compiler, so that the Makefile works in
# every MSYS2 environment (mingw64, ucrt64, clang64, ...).
CXX_BIN_DIR  := $(dir $(shell command -v $(CXX) 2>/dev/null))
CXX_LIB_DIR  := $(patsubst %/,%,$(dir $(CXX_BIN_DIR)))lib

LDFLAGS := -shared -static-libgcc -static-libstdc++ -lcomctl32 -lgdi32 \
           -Wl,--whole-archive $(CXX_LIB_DIR)/libwinpthread.a -Wl,--no-whole-archive

TARGET  := MarkdownOutline.dll
SOURCES := MarkdownOutline.cpp OutlinePanel.cpp MarkdownParser.cpp
OBJECTS := $(SOURCES:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

MarkdownOutline.o: MarkdownOutline.cpp MarkdownParser.h OutlinePanel.h
OutlinePanel.o: OutlinePanel.cpp OutlinePanel.h MarkdownParser.h
MarkdownParser.o: MarkdownParser.cpp MarkdownParser.h

clean:
	rm -f $(OBJECTS) $(TARGET)
