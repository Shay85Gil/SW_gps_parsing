# Makefile — NMEA GPS parser
# Supports Linux (gcc/clang) and Windows (MinGW g++)

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic

ifeq ($(OS),Windows_NT)
    TARGET := nmea_parser.exe
    RM     := del /Q
else
    TARGET := nmea_parser
    RM     := rm -f
endif

SRCS := main.cpp nmea_parser.cpp dedup.cpp output.cpp
OBJS := $(SRCS:.cpp=.o)
HDRS := gpsd.h nmea_parser.h dedup.h output.h

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	$(RM) $(OBJS) $(TARGET)
