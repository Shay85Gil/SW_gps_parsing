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

SRCS := main.cpp
OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp gpsd.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	$(RM) $(OBJS) $(TARGET)
