# Makefile for Suika4D

BUILD_DIR = build
CMAKE = cmake

.PHONY: all build run clean package

all: build

build:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD_DIR) --config Release -j 8

run: build
	./$(BUILD_DIR)/4d_game

clean:
	rm -rf $(BUILD_DIR)

package: build
	cd $(BUILD_DIR) && cpack -C Release
