# Makefile for Engine Core Config System
# Requires C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 2019+)

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2
LDFLAGS = -lstdc++fs

# Source files
SOURCES = Config.cpp
EXAMPLE_SOURCES = config_example.cpp Config.cpp

# Object files
OBJECTS = $(SOURCES:.cpp=.o)
EXAMPLE_OBJECTS = $(EXAMPLE_SOURCES:.cpp=.o)

# Targets
all: example

example: $(EXAMPLE_OBJECTS)
	$(CXX) $(CXXFLAGS) -o config_example $(EXAMPLE_OBJECTS) $(LDFLAGS)

config_test: config_test.cpp Config.o
	$(CXX) $(CXXFLAGS) -o config_test config_test.cpp Config.o $(LDFLAGS)

# Compile object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f *.o config_example config_test game_config_modified.json

# Run the example
run: example
	./config_example

.PHONY: all clean run
