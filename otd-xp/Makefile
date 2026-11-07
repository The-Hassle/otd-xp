# Cross-compile for Windows XP (32-bit) with mingw-w64:  make
CC := $(shell command -v i686-w64-mingw32-gcc-win32 2>/dev/null || echo i686-w64-mingw32-gcc)
CFLAGS = -std=gnu99 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -Isrc
# PE header says "Windows 5.1" so XP accepts the file; everything is linked statically
LDFLAGS = -static -static-libgcc -s \
	-Wl,--major-os-version,5 -Wl,--minor-os-version,1 \
	-Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,1
LIBS = -lsetupapi

all: otd-xp.exe

otd-xp.exe: src/main_win.c src/core.c src/core.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ src/main_win.c src/core.c $(LIBS)

test: tests/test_core.c src/core.c src/core.h tablets.txt
	gcc -std=c99 -O1 -Wall -Wextra -o test_core tests/test_core.c src/core.c -lm
	./test_core tablets.txt

tablets.txt:
	python3 tools/gen_tablets.py $(OTD_CONFIGS) tablets.txt

clean:
	rm -f otd-xp.exe test_core
