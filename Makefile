CXX := xcrun clang++
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra
PREFIX_MAP := -ffile-prefix-map=$(CURDIR)=/WhateverXe-Next
HEALTH_HEADERS := src/GPUHealth.hpp src/RecoveryState.hpp
HALT_HEADERS := driver/WhateverXe/WhateverXe/kern_tgl_hang_cpu.hpp driver/WhateverXe/WhateverXe/kern_tgl_hang_pending.hpp
.PHONY: all cpu-test clean
all: build/metal-memory-check build/metal-async-check build/gl-blit-check build/posh-lifetime build/bounded-count build/tgl-capture build/posh-queue-check build/metal-surface-check build/posh-concurrent-gpu build/posh-atomic-code build/posh-abandon-check build/recovery-state build/capture-reports

build:
	mkdir -p build

build/POSHDeferred.o: src/POSHDeferred.mm src/POSHDeferred.h src/POSHLifetime.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fno-objc-arc -Wno-deprecated-declarations -c $< -o $@

build/POSHQueueCache.o: src/POSHQueueCache.mm src/POSHQueueCache.h | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fno-objc-arc -Wno-deprecated-declarations -c $< -o $@

build/POSHAtomicClaim.o: src/POSHAtomicClaim.mm src/POSHAtomicClaim.h src/POSHAtomicCode.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fno-objc-arc -Wno-deprecated-declarations -c $< -o $@

build/metal-memory-check: tests/metal-memory-check.mm build/POSHDeferred.o build/POSHQueueCache.o src/BoundedCount.hpp $(HEALTH_HEADERS) | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fobjc-arc $< build/POSHDeferred.o build/POSHQueueCache.o -framework Foundation -framework Metal -framework IOKit -o $@

build/metal-async-check: tests/metal-async-check.mm build/POSHDeferred.o build/POSHQueueCache.o src/BoundedCount.hpp $(HEALTH_HEADERS) | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fobjc-arc $< build/POSHDeferred.o build/POSHQueueCache.o -framework Foundation -framework Metal -framework IOKit -o $@

build/posh-queue-check: tests/posh-queue-check.mm build/POSHQueueCache.o build/POSHAtomicClaim.o $(HEALTH_HEADERS) | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fno-objc-arc $< build/POSHQueueCache.o build/POSHAtomicClaim.o -framework Foundation -framework Metal -framework IOKit -o $@

build/posh-abandon-check: tests/posh-abandon-check.mm build/POSHQueueCache.o build/POSHAtomicClaim.o $(HEALTH_HEADERS) | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fno-objc-arc $< build/POSHQueueCache.o build/POSHAtomicClaim.o -framework Foundation -framework Metal -framework IOKit -o $@

build/posh-concurrent-gpu: tests/posh-concurrent-gpu.mm build/POSHQueueCache.o build/POSHAtomicClaim.o src/BoundedCount.hpp $(HEALTH_HEADERS) | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fno-objc-arc $< build/POSHQueueCache.o build/POSHAtomicClaim.o -framework Foundation -framework Metal -framework IOKit -o $@

build/metal-surface-check: tests/metal-surface-check.mm build/POSHQueueCache.o build/POSHAtomicClaim.o src/BoundedCount.hpp $(HEALTH_HEADERS) | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fobjc-arc $< build/POSHQueueCache.o build/POSHAtomicClaim.o -framework Foundation -framework Metal -framework CoreVideo -framework IOKit -framework AppKit -framework QuartzCore -o $@

build/tgl-capture: tools/tgl-capture.mm src/RecoveryState.hpp $(HALT_HEADERS) | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fobjc-arc $< -framework Foundation -framework IOKit -o $@

build/gl-blit-check: tests/gl-blit-check.mm build/POSHDeferred.o src/BoundedCount.hpp $(HEALTH_HEADERS) | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fobjc-arc -Wno-deprecated-declarations $< build/POSHDeferred.o -framework AppKit -framework Metal -framework OpenGL -framework IOKit -o $@

build/posh-lifetime: tests/posh-lifetime.cpp src/POSHLifetime.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $< -o $@

build/bounded-count: tests/bounded-count.cpp src/BoundedCount.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -O1 -fsanitize=address,undefined $< -o $@

build/posh-atomic-code: tests/posh-atomic-code.cpp src/POSHAtomicCode.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $< -o $@

build/recovery-state: tests/recovery-state.cpp src/RecoveryState.hpp | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -O1 -fsanitize=address,undefined $< -o $@

build/capture-reports: tests/capture-reports.mm tools/tgl-capture.mm src/RecoveryState.hpp $(HALT_HEADERS) | build
	$(CXX) $(CXXFLAGS) $(PREFIX_MAP) -fobjc-arc -fsanitize=address,undefined $< -framework Foundation -framework IOKit -o $@

cpu-test: build/posh-lifetime build/bounded-count build/posh-atomic-code build/recovery-state build/capture-reports
	./build/posh-lifetime
	./build/bounded-count
	./build/posh-atomic-code
	./build/recovery-state
	./build/capture-reports

clean:
	rm -rf build
