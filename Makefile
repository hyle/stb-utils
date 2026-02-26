CC     = clang
CFLAGS = -O2 -Ivendor/stb -Wall -Wextra
BUILD  = build
OBJ    = $(BUILD)/stb_impl.o
BIN    = $(BUILD)/stb-noise
SAN_OBJ = $(BUILD)/stb_impl_sanitize.o
SAN_BIN = $(BUILD)/stb-noise-sanitize
SAN_CFLAGS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Ivendor/stb -Wall -Wextra

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/stb_impl.o: src/stb_impl.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): src/noise/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test: $(BIN)
	sh tests/test_cli.sh

sanitize: $(SAN_BIN)
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 BIN=$(SAN_BIN) sh tests/test_cli.sh

$(SAN_OBJ): src/stb_impl.c | $(BUILD)
	$(CC) $(SAN_CFLAGS) -c $< -o $@

$(SAN_BIN): src/noise/main.c $(SAN_OBJ) | $(BUILD)
	$(CC) $(SAN_CFLAGS) $^ -o $@ -lm

clean:
	rm -rf $(BUILD)

.PHONY: all test sanitize clean
