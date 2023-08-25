CFLAGS = -O3 -std=c11 -pedantic -D_POSIX_C_SOURCE=200809L -D_REENTRANT \
	 -I/usr/include/SDL2

WFLAGS = -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wno-switch \
	 -Wmissing-declarations -Werror=implicit -Wdeclaration-after-statement \
	 -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wrestrict \
	 -Wnull-dereference -Wjump-misses-init -Wdouble-promotion -Wshadow \
	 -Wformat=2 -Wstrict-aliasing -Wno-unknown-warning-option \
	 -Wno-format-nonliteral -Wpedantic

LFLAGS = -lSDL2 -lSDL2_ttf -lavcodec -lavutil -lavformat

all: ircam util/kfwd
debug: CFLAGS += -g -Og -fsanitize=address
debug: all

disasm: CFLAGS += -fverbose-asm
disasm: main.s sdl.s v4l2.s lavc.s fontcache.s

sdl.s: gamma.h
sdl.o: gamma.h
ircam: main.o sdl.o v4l2.o lavc.o fontcache.o
	$(CC) -o $@ $^ $(CFLAGS) $(LFLAGS)

util/kfwd: util/kfwd.o
	$(CC) -o $@ $^ $(CFLAGS)

gamma.h:
	./util/mkgamma.py > gamma.h

%.o: %.c
	$(CC) $< $(CFLAGS) $(WFLAGS) -c -o $@

%.s: %.c
	$(CC) $< $(CFLAGS) -c -S -o $@

# Don't enable any warnings for FC_FontCache
fontcache.o: fontcache.c
	$(CC) $< $(CFLAGS) -c -o $@

clean:
	rm -f ircam util/kfwd *.mkv *.o *.s util/*.o util/*.s gamma.h
