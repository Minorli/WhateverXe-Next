CXX := xcrun clang++
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra
PREFIX_MAP := -ffile-prefix-map=$(CURDIR)=/WhateverXe-Next
.PHONY: all cpu-test clean
all: build/metal-memory-check build/metal-async-check build/gl-blit-check build/posh-lifetime build/bounded-count build/tgl-capture

build:
	mkdir -p build

build/POSHDeferred.o: src/POSHDeferred.mm src/POSHDeferred.h src/POSHLifetime.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fno-objc-arc -Wno-deprecated-declarations -c $< -o $@

build/metal-memory-check: tests/metal-memory-check.mm build/POSHDeferred.o src/BoundedCount.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fobjc-arc $< build/POSHDeferred.o -framework Foundation -framework Metal -o $@

build/metal-async-check: tests/metal-async-check.mm build/POSHDeferred.o src/BoundedCount.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fobjc-arc $< build/POSHDeferred.o -framework Foundation -framework Metal -o $@

build/tgl-capture: tools/tgl-capture.mm | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fobjc-arc $< -framework Foundation -framework IOKit -o $@

build/gl-blit-check: tests/gl-blit-check.mm build/POSHDeferred.o src/BoundedCount.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fobjc-arc -Wno-deprecated-declarations $< build/POSHDeferred.o -framework AppKit -framework Metal -framework OpenGL -o $@

build/posh-lifetime: tests/posh-lifetime.cpp src/POSHLifetime.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $< -o $@

build/bounded-count: tests/bounded-count.cpp src/BoundedCount.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -O1 -fsanitize=address,undefined $< -o $@

cpu-test: build/posh-lifetime build/bounded-count
	./build/posh-lifetime
	./build/bounded-count

clean:
	rm -rf build
