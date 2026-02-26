CC     = clang
CFLAGS = -O2 -Ivendor/stb -Wall -Wextra
BUILD  = build
OBJ    = $(BUILD)/stb_impl.o

# Release Binaries
BIN_NOISE = $(BUILD)/stb-noise
BIN_ATLAS = $(BUILD)/stb-atlas

# Sanitizer Setup
SAN_CFLAGS  = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Ivendor/stb -Wall -Wextra
SAN_OBJ     = $(BUILD)/stb_impl_sanitize.o
SAN_BIN_NOISE = $(BUILD)/stb-noise-sanitize
SAN_BIN_ATLAS = $(BUILD)/stb-atlas-sanitize

all: $(BIN_NOISE) $(BIN_ATLAS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/stb_impl.o: src/stb_impl.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Release Builds ---
$(BIN_NOISE): src/noise/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(BIN_ATLAS): src/atlas/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ -lm

# --- Testing & Sanitization ---
test: $(BIN_NOISE) $(BIN_ATLAS)
	BIN=$(BIN_NOISE) sh tests/test_cli.sh
	BIN=$(BIN_ATLAS) sh tests/test_atlas.sh

sanitize: $(SAN_BIN_NOISE) $(SAN_BIN_ATLAS)
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
	  BIN=$(SAN_BIN_NOISE) sh tests/test_cli.sh
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
	  BIN=$(SAN_BIN_ATLAS) sh tests/test_atlas.sh

$(SAN_OBJ): src/stb_impl.c | $(BUILD)
	$(CC) $(SAN_CFLAGS) -c $< -o $@

$(SAN_BIN_NOISE): src/noise/main.c $(SAN_OBJ) | $(BUILD)
	$(CC) $(SAN_CFLAGS) $^ -o $@ -lm

$(SAN_BIN_ATLAS): src/atlas/main.c $(SAN_OBJ) | $(BUILD)
	$(CC) $(SAN_CFLAGS) $^ -o $@ -lm

clean:
	rm -rf $(BUILD)

.PHONY: all test sanitize clean
