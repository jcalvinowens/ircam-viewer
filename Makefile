CC ?= gcc

CFLAGS ?= -O3
BASE_CFLAGS = -std=c11 -pedantic -D_GNU_SOURCE -D_REENTRANT
CFLAGS += $(BASE_CFLAGS)

WFLAGS = -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wno-switch \
	 -Wmissing-declarations -Werror=implicit -Wdeclaration-after-statement \
	 -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wrestrict \
	 -Wnull-dereference -Wjump-misses-init -Wdouble-promotion -Wshadow \
	 -Wformat=2 -Wstrict-aliasing -Wno-unknown-warning-option \
	 -Wno-format-nonliteral -Wpedantic

all: ircam util/kfwd
nosdl: ircam-nosdl

debug: CFLAGS := -g -Og -fsanitize=address $(BASE_CFLAGS)
debug: all

FMTSRCS = inet.c inet.h lavc.c lavc.h main.c sdl.c sdl.h v4l2.c v4l2.h \
	  util/kfwd.c

format:
	clang-format -i $(FMTSRCS)

sdl.s: gamma.h
sdl.o: gamma.h

ircam: main.o v4l2.o lavc.o inet.o sdl.o fontcache.o builtin.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) -lSDL2 -lSDL2_ttf -lavcodec -lavutil -lavformat

ircam-nosdl: CFLAGS += -DIRCAM_NOSDL -Wno-unused-parameter
ircam-nosdl: main.o v4l2.o lavc.o inet.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) -lavcodec -lavutil -lavformat

util/kfwd: util/kfwd.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

gamma.h:
	./util/gamma.py > gamma.h

%.o: %.c
	$(CC) $< $(CFLAGS) $(WFLAGS) -c -o $@

%.s: %.c
	$(CC) $< $(CFLAGS) -fverbose-asm $(WFLAGS) -S -c -o $@

%.o: %.s
	$(CC) $< -c -o $@

# Don't enable any warnings for FC_FontCache
fontcache.o: fontcache.c
	$(CC) $< $(CFLAGS) -c -o $@

clean:
	rm -f ircam ircam-nosdl util/kfwd *.o fonts/*.o util/*.o
