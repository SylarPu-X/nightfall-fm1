CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -pedantic
BUILD := build
SRC := src/core.c src/fx.c src/city.c src/project.c src/protocol.c src/link.c src/store.c

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test: $(SRC) tests/test.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(SRC) tests/test.c -o $@

$(BUILD)/preview: $(SRC) tests/preview.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(SRC) tests/preview.c -o $@

$(BUILD)/render_bank: $(SRC) tests/render_bank.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(SRC) tests/render_bank.c -o $@

.PHONY: test preview audio-test clean
test: $(BUILD)/test
	./$(BUILD)/test

preview: $(BUILD)/preview
	./$(BUILD)/preview > $(BUILD)/city.ppm
	./$(BUILD)/preview fx > $(BUILD)/fx.ppm
	./$(BUILD)/preview edit 2 > $(BUILD)/edit-after-rain.ppm
	./$(BUILD)/preview scope > $(BUILD)/terrain.ppm

audio-test: $(BUILD)/render_bank
	./$(BUILD)/render_bank 0 > $(BUILD)/midnight.wav
	./$(BUILD)/render_bank 1 > $(BUILD)/neon-run.wav
	./$(BUILD)/render_bank 2 > $(BUILD)/after-rain.wav
	python3 tools/check_demo.py $(BUILD)/midnight.wav $(BUILD)/neon-run.wav $(BUILD)/after-rain.wav

clean:
	rm -f $(BUILD)/test $(BUILD)/preview $(BUILD)/render_bank $(BUILD)/city.ppm $(BUILD)/fx.ppm $(BUILD)/edit-after-rain.ppm
