CC=cc
CFLAGS= -g -Wall 
LDLIBS= -lm

SRCS=src/vi.c \
    src/term.c \
    src/std.c \
    src/socket.c \
    src/file.c \
    src/folder.c

OBJ_EXT=.o
OBJS= ${patsubst src/%,build/%,$(SRCS:.c=$(OBJ_EXT))}

all: build build/vi
	@echo "done"

build/vi: $(OBJS)
	$(CC) $^ $(LDLIBS) -o $@

build:
	@mkdir -p build

clean:
	rm -f vi a.out *.log *.o *.obj
	rm -f build/*

fmt:
	clang-format --style="{BasedOnStyle: llvm,UseTab: Always,IndentWidth: 8,TabWidth: 8}" -i src/vi.c

.SUFFIXES:
.SUFFIXES: .c .o
build/%.o: src/%.c
	$(CC) -c $(CFLAGS) $< -o $@
