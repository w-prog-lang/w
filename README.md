# W PROGRAMMING LANGUAGE

> [!WARNING]
> The Compiler is in still development, it is not stable.

## Building

Requires `gcc` and `make`. On Windows, use [MinGW](https://www.mingw-w64.org/), [MSYS2](https://www.msys2.org/), or Cygwin.

---

## Make Targets

### Default build
```sh
make
```
Compiles all `.c` files in `src/` and outputs the binary to `build/wlang/wlang` (Linux/macOS) or `build\wlang\wlang.exe` (Windows).

### Debug build
```sh
make debug
```
Builds with debug symbols and no optimisation (`-g -DDEBUG -O0`). Use with `gdb` or `lldb`.

### Release build
```sh
make release
```
Builds with full optimisation and assertions disabled (`-O3 -DNDEBUG`).

### Run
```sh
make run
```
Builds (if needed) then immediately runs the binary.

### Show build info
```sh
make info
```
Prints the detected platform, compiler, flags, source files, and output path.

---

## Cleanup

```sh
make clean
```
Deletes the entire `build/` directory.

```sh
make distclean
```
Everything `clean` does, plus removes stray `.o` files, macOS `.dSYM` bundles, swap files, and editor backups from the project root.
