# Makefile — NMEA GPS parser
# Supports Linux (gcc/clang) and Windows (MinGW g++)

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic
BUILDDIR := build

ifeq ($(OS),Windows_NT)
    TARGET := $(BUILDDIR)/nmea_parser.exe
    MKDIR  := if not exist $(BUILDDIR) mkdir $(BUILDDIR)
    RM     := rmdir /S /Q $(BUILDDIR)
else
    TARGET := $(BUILDDIR)/nmea_parser
    MKDIR  := mkdir -p $(BUILDDIR)
    RM     := rm -rf $(BUILDDIR)
endif

SRCS := main.cpp nmea_parser.cpp dedup.cpp output.cpp
OBJS := $(patsubst %.cpp,$(BUILDDIR)/%.o,$(SRCS))
HDRS := gpsd.h nmea_parser.h dedup.h output.h

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILDDIR)/%.o: %.cpp $(HDRS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR):
	$(MKDIR)

clean:
	$(RM)
