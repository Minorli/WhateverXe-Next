# TGLDisplayProbe: standalone x86_64 kext + host CPU tests.
# make            -> build/TGLDisplayProbe.kext
# make test       -> CPU-only logic tests (ASan/UBSan)
# make fb-shared  -> build/fb-shared-open (triggers WEX on-demand publication)
SDK      ?= $(shell xcrun --sdk macosx --show-sdk-path)
CLANG    ?= $(shell xcrun -f clang++)
CC_KEXT  ?= $(shell dirname $$(xcrun -f clang))/../lib/clang/$(shell xcrun clang -dumpversion | cut -d. -f1)/lib/darwin/libclang_rt.cc_kext.a
OUT      := build
MIN_OS   ?= $(shell xcrun --sdk macosx --show-sdk-version)
KEXT     := $(OUT)/TGLDisplayProbe.kext
KCOMMON  := -arch x86_64 -mmacosx-version-min=$(MIN_OS) -isysroot $(SDK) \
            -isystem $(SDK)/System/Library/Frameworks/Kernel.framework/Headers \
            -mkernel -nostdlib -fno-builtin -fno-common -fno-exceptions -fno-rtti \
            -fno-stack-protector -DKERNEL -DKERNEL_PRIVATE=0 -DDRIVER_PRIVATE -DAPPLE -DNeXT \
            -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-deprecated-declarations \
            -ffile-prefix-map=$(CURDIR)=/TGLDisplayProbe
KFLAGS   := $(KCOMMON) -fapple-kext
.PHONY: all test fb-shared clean
all: $(KEXT)

$(OUT):
	mkdir -p $(OUT)

$(OUT)/TGLDisplayProbe.o: TGLDisplayProbe.cpp TGLDisplayProbeLogic.hpp | $(OUT)
	$(CLANG) $(KFLAGS) -std=gnu++17 -c $< -o $@

$(OUT)/kmod_info.o: kmod_info.c | $(OUT)
	$(CLANG) -x c $(KCOMMON) -c $< -o $@

$(KEXT): $(OUT)/TGLDisplayProbe.o $(OUT)/kmod_info.o Info.plist
	mkdir -p $(KEXT)/Contents/MacOS
	$(CLANG) -arch x86_64 -mmacosx-version-min=$(MIN_OS) -isysroot $(SDK) -nostdlib -Xlinker -kext \
	    $(OUT)/TGLDisplayProbe.o $(OUT)/kmod_info.o -L$(SDK)/usr/lib -lkmodc++ -lkmod $(CC_KEXT) \
	    -o $(KEXT)/Contents/MacOS/TGLDisplayProbe
	cp Info.plist $(KEXT)/Contents/Info.plist
	shasum -a 256 $(KEXT)/Contents/MacOS/TGLDisplayProbe

test: | $(OUT)
	xcrun clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined tests/logic_test.cpp -o $(OUT)/logic_test
	$(OUT)/logic_test

fb-shared: | $(OUT)
	xcrun clang -O2 -Wall -Wextra -Werror tools/fb-shared-open.c -framework IOKit -framework CoreFoundation -o $(OUT)/fb-shared-open

clean:
	rm -rf $(OUT)
