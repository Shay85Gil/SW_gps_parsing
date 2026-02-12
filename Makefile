# Makefile — NMEA GPS parser
# Supports Linux (gcc/clang) and Windows (MinGW g++)
#
# Source layout:
#   src/<module>/ — library modules (auto-discovered via wildcard)
#   app/         — application entry point (main.cpp)
#
# Convention: each module lives in its own src/<module>/ directory
# containing its .h and .cpp files. To add a new module, create
# src/<name>/<name>.h and src/<name>/<name>.cpp — no Makefile edits needed.

CXX      := g++
BUILDDIR := build

# Auto-discover every module subdirectory and add it as an include path,
# so both bare (#include "nmea_parser.h") and qualified
# (#include "nmea_parser/nmea_parser.h") includes resolve correctly.
SRC_DIRS := $(wildcard src/*/)
INCLUDES := -Isrc $(addprefix -I,$(SRC_DIRS))
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic $(INCLUDES)

ifeq ($(OS),Windows_NT)
    TARGET := $(BUILDDIR)/nmea_parser.exe
    RM     := rmdir /S /Q $(BUILDDIR)
else
    TARGET := $(BUILDDIR)/nmea_parser
    RM     := rm -rf $(BUILDDIR)
endif

# Auto-discover every .cpp under src/ (any depth) and all app sources.
SRC_SRCS := $(wildcard src/*/*.cpp)
APP_SRCS := app/main.cpp

# Map source paths to build paths, preserving directory structure.
SRC_OBJS := $(patsubst src/%.cpp,$(BUILDDIR)/src/%.o,$(SRC_SRCS))
APP_OBJS := $(patsubst app/%.cpp,$(BUILDDIR)/app/%.o,$(APP_SRCS))
OBJS     := $(APP_OBJS) $(SRC_OBJS)

# Auto-discover all headers for dependency tracking.
HDRS := $(wildcard src/*/*.h)

# Collect every build subdirectory that needs to exist.
BUILD_DIRS := $(sort $(dir $(OBJS)))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(BUILDDIR)/src/%.o: src/%.cpp $(HDRS) | $(BUILD_DIRS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/app/%.o: app/%.cpp $(HDRS) | $(BUILD_DIRS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIRS) $(BUILDDIR):
	mkdir -p $@

clean:
	$(RM)
