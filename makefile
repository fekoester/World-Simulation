CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
CPPFLAGS += -Iinclude
SFML_LIBS = -lsfml-graphics -lsfml-window -lsfml-system
HEADERS = $(wildcard include/wsim/*.hpp) third_party/PerlinNoise.hpp

all: wsim

wsim: src/main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $< $(SFML_LIBS)

# no SFML needed
headless: src/main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DWSIM_NO_SFML -o wsim-headless $<

wsim_tests: tests/test_wsim.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DWSIM_SOURCE_DIR='"$(CURDIR)"' -o $@ $<

test: wsim_tests
	./wsim_tests

clean:
	rm -f wsim wsim-headless wsim_tests

.PHONY: all headless test clean
