# Makefile for Suika4D

BUILD_DIR = build
CMAKE = cmake
NICE = nice -n 10
JOBS = 4

.PHONY: all build run clean package

all: build

build:
	$(NICE) $(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(NICE) $(CMAKE) --build $(BUILD_DIR) --config Release -j $(JOBS)

run: build
	./$(BUILD_DIR)/4d_game.app/Contents/MacOS/4d_game

clean:
	rm -rf $(BUILD_DIR)

package: build
	cd $(BUILD_DIR) && cpack -C Release
