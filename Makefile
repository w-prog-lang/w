# ============================================================
#  wlang — Cross-platform Makefile
#  Targets: Linux, macOS, Windows (MinGW / MSYS2 / Cygwin)
# ============================================================

# ---------- Toolchain -------------------------------------------
CC      := gcc
CFLAGS  := -Wall -Wextra -pedantic -std=c11 -O2
LDFLAGS := -ltcc

# ---------- Detect OS -------------------------------------------
ifeq ($(OS),Windows_NT)
    PLATFORM   := windows
    TARGET_BIN := wlang.exe
    RM         := cmd /C "if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)""
    MKDIR      := cmd /C "if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)""
    SEP        := \\
else
    UNAME := $(shell uname -s)
    ifeq ($(UNAME),Darwin)
        PLATFORM := macos
    else
        PLATFORM := linux
    endif
    TARGET_BIN := wlang
    RM         := rm -rf
    MKDIR      := mkdir -p
    SEP        := /
endif

# ---------- Directories -----------------------------------------
SRC_DIR   := src
BUILD_DIR := build
OUT_DIR   := $(BUILD_DIR)$(SEP)wlang

# ---------- Sources & Objects -----------------------------------
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# ---------- Final binary ----------------------------------------
TARGET := $(OUT_DIR)$(SEP)$(TARGET_BIN)

# ================================================================
#  Default target
# ================================================================
.PHONY: all
all: $(TARGET)
	@echo ""
	@echo "  Build complete  →  $(TARGET)"
	@echo "  Platform        →  $(PLATFORM)"
	@echo ""

# ---------- Link ------------------------------------------------
$(TARGET): $(OBJS)
ifeq ($(OS),Windows_NT)
	@cmd /C "if not exist "$(subst /,\,$(OUT_DIR))" mkdir "$(subst /,\,$(OUT_DIR))""
else
	@$(MKDIR) $(OUT_DIR)
endif
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# ---------- Compile ---------------------------------------------
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
ifeq ($(OS),Windows_NT)
	@cmd /C "if not exist "$(subst /,\,$(BUILD_DIR))" mkdir "$(subst /,\,$(BUILD_DIR))""
else
	@$(MKDIR) $(BUILD_DIR)
endif
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

# ================================================================
#  Optional build variants
# ================================================================
.PHONY: debug
debug: CFLAGS += -g -DDEBUG -O0
debug: all

.PHONY: release
release: CFLAGS += -O3 -DNDEBUG
release: all

# ================================================================
#  Clean
# ================================================================
.PHONY: clean
clean:
ifeq ($(OS),Windows_NT)
	@cmd /C "if exist "$(subst /,\,$(BUILD_DIR))" rmdir /S /Q "$(subst /,\,$(BUILD_DIR))""
else
	$(RM) $(BUILD_DIR)
endif
	@echo "  Cleaned build directory."

# Full clean — also removes any editor/OS clutter in the project root
.PHONY: distclean
distclean: clean
ifeq ($(OS),Windows_NT)
	@cmd /C "del /Q /F *.o *.exe 2>nul || exit 0"
else
	$(RM) *.o
	find . -name "*.dSYM" -exec rm -rf {} + 2>/dev/null || true
	find . -name "*.swp"  -delete 2>/dev/null || true
	find . -name "*~"     -delete 2>/dev/null || true
endif
	@echo "  Full distclean complete."

# ================================================================
#  Helpers
# ================================================================
.PHONY: run
run: all
	./$(TARGET)

.PHONY: info
info:
	@echo "Platform  : $(PLATFORM)"
	@echo "Compiler  : $(CC)"
	@echo "CFLAGS    : $(CFLAGS)"
	@echo "Sources   : $(SRCS)"
	@echo "Objects   : $(OBJS)"
	@echo "Target    : $(TARGET)"
