CC              ?= cc
BUILD           ?= build

STD_CFLAGS      ?= -std=c99
WARN_CFLAGS     ?= -Wall -Wextra
CPPFLAGS        ?= -Ivendor/stb
CFLAGS          ?= -O2
LDFLAGS         ?=
LDLIBS          ?= -lm

SAN_FLAGS       ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
SAN_LDFLAGS     ?= -fsanitize=address,undefined

REL_ALL_CFLAGS  = $(STD_CFLAGS) $(WARN_CFLAGS) $(CPPFLAGS) $(CFLAGS)
SAN_ALL_CFLAGS  = $(STD_CFLAGS) $(WARN_CFLAGS) $(CPPFLAGS) $(SAN_FLAGS)
SAN_ALL_LDFLAGS = $(LDFLAGS) $(SAN_LDFLAGS)

OBJ             := $(BUILD)/stb_impl.o
NOISE_OBJ       := $(BUILD)/noise_main.o
ATLAS_OBJ       := $(BUILD)/atlas_main.o

SAN_OBJ         := $(BUILD)/stb_impl_sanitize.o
SAN_NOISE_OBJ   := $(BUILD)/noise_main_sanitize.o
SAN_ATLAS_OBJ   := $(BUILD)/atlas_main_sanitize.o

BIN_NOISE       := $(BUILD)/stb-noise
BIN_ATLAS       := $(BUILD)/stb-atlas
SAN_BIN_NOISE   := $(BUILD)/stb-noise-sanitize
SAN_BIN_ATLAS   := $(BUILD)/stb-atlas-sanitize

DEPFILES        := $(OBJ:.o=.d) $(NOISE_OBJ:.o=.d) $(ATLAS_OBJ:.o=.d) \
                   $(SAN_OBJ:.o=.d) $(SAN_NOISE_OBJ:.o=.d) $(SAN_ATLAS_OBJ:.o=.d)

all: $(BIN_NOISE) $(BIN_ATLAS)

$(BUILD):
	mkdir -p $@

$(OBJ): src/stb_impl.c | $(BUILD)
	$(CC) $(REL_ALL_CFLAGS) -MMD -MP -c $< -o $@

$(NOISE_OBJ): src/noise/main.c | $(BUILD)
	$(CC) $(REL_ALL_CFLAGS) -MMD -MP -c $< -o $@

$(ATLAS_OBJ): src/atlas/main.c | $(BUILD)
	$(CC) $(REL_ALL_CFLAGS) -MMD -MP -c $< -o $@

$(BIN_NOISE): $(NOISE_OBJ) $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BIN_ATLAS): $(ATLAS_OBJ) $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(SAN_OBJ): src/stb_impl.c | $(BUILD)
	$(CC) $(SAN_ALL_CFLAGS) -MMD -MP -c $< -o $@

$(SAN_NOISE_OBJ): src/noise/main.c | $(BUILD)
	$(CC) $(SAN_ALL_CFLAGS) -MMD -MP -c $< -o $@

$(SAN_ATLAS_OBJ): src/atlas/main.c | $(BUILD)
	$(CC) $(SAN_ALL_CFLAGS) -MMD -MP -c $< -o $@

$(SAN_BIN_NOISE): $(SAN_NOISE_OBJ) $(SAN_OBJ)
	$(CC) $(SAN_ALL_LDFLAGS) -o $@ $^ $(LDLIBS)

$(SAN_BIN_ATLAS): $(SAN_ATLAS_OBJ) $(SAN_OBJ)
	$(CC) $(SAN_ALL_LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(BIN_NOISE) $(BIN_ATLAS)
	BIN=$(BIN_NOISE) sh tests/test_cli.sh
	BIN=$(BIN_ATLAS) sh tests/test_atlas.sh

sanitize: $(SAN_BIN_NOISE) $(SAN_BIN_ATLAS)
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		BIN=$(SAN_BIN_NOISE) sh tests/test_cli.sh
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		BIN=$(SAN_BIN_ATLAS) sh tests/test_atlas.sh

clean:
	rm -rf $(BUILD)

-include $(DEPFILES)

.PHONY: all test sanitize clean
