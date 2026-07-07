CC = clang
CFLAGS = -std=c11 -Wall -Wextra -g
SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)

build/wlangc: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

test: build/wlangc
	./test/run_tests.sh

clean:
	rm -rf build

.PHONY: test clean
