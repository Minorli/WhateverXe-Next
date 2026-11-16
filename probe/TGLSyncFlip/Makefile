# TGLSyncFlip: standalone Lilu plugin for a same-boot async/sync flip A/B.
# make            -> build/TGLSyncFlip.kext (needs MacKernelSDK + Lilu 1.7.2 DEBUG SDK)
# make test       -> CPU-only logic tests (ASan/UBSan); FB=path also checks a real framebuffer
# make ctl        -> build/tglsync-ctl
SDK  ?= ../../driver/WhateverXe/MacKernelSDK
LILU ?= ../../driver/WhateverXe/Lilu.kext
FB   ?=
.PHONY: all test ctl clean
all:
	python3 build.py --sdk $(SDK) --lilu $(LILU)
test:
	mkdir -p build
	xcrun clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined tests/logic_test.cpp -o build/logic_test
	./build/logic_test $(if $(FB),--framebuffer $(FB))
	python3 tests/build_abi_test.py --sdk $(SDK)
ctl:
	mkdir -p build
	xcrun clang++ -std=c++17 -O2 -Wall -Wextra -Werror tools/tglsync-ctl.cpp -framework IOKit -framework CoreFoundation -o build/tglsync-ctl
clean:
	rm -rf build
