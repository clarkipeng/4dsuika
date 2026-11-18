# Makefile for Suika4D

BUILD_DIR = build
CMAKE = cmake
NICE = nice -n 10
JOBS = 4

.PHONY: all build run clean package video gameplay turntable view-compare

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

# Stitch recorded frames into an mp4: `make video` (see docs/recording.md).
FRAMES ?= recordings/frames
FPS ?= 60
VIDEO ?= recordings/trailer.mp4
FFMPEG ?= ffmpeg

video:
	$(FFMPEG) -y -framerate $(FPS) -i $(FRAMES)/frame_%06d.png \
		-c:v libx264 -preset slow -crf 14 -pix_fmt yuv420p -movflags +faststart $(VIDEO)

# Gameplay for the store page: a scripted player, 20 s at 1080p30 (see docs/recording.md).
GAMEPLAY_DIR ?= recordings/gameplay

gameplay:
	SDL_AUDIODRIVER=dummy ./$(BUILD_DIR)/4d_game.app/Contents/MacOS/4d_game --headless --mute \
		--record $(GAMEPLAY_DIR) --camera-path docs/recording/paths/gameplay.path --play 0.9 --warmup 30 \
		--seconds 20 --fps 30 --size 1920x1080
	$(FFMPEG) -nostdin -loglevel error -y -framerate 30 -i $(GAMEPLAY_DIR)/frame_%06d.png -c:v libx264 -preset slow \
		-crf 16 -pix_fmt yuv420p -movflags +faststart $(GAMEPLAY_DIR).mp4

# 4D review turntable: a video and a contact sheet per subject in TURNTABLE_DIR
# (see docs/recording.md). SUBJECT is all, fruit, bowl or fruit names, comma separated.
TURNTABLE_DIR ?= recordings/turntable
SUBJECT ?= all

turntable:
	SDL_AUDIODRIVER=dummy ./$(BUILD_DIR)/4d_game.app/Contents/MacOS/4d_game --headless --mute \
		--turntable $(TURNTABLE_DIR) --subject $(SUBJECT) --size 960x960 --fps 30
	for d in $(TURNTABLE_DIR)/*/; do \
		$(FFMPEG) -nostdin -loglevel error -y -framerate 30 -i $${d}frame_%06d.png -c:v libx264 -preset slow \
			-crf 16 -pix_fmt yuv420p -movflags +faststart $${d%/}.mp4 || exit 1; \
	done

# Every view and combination (docs/4D.md, "View modes"): view-<v>.png, view-<v>.mp4 and compare.png in COMPARE_DIR.
COMPARE_DIR ?= recordings/view-compare

view-compare:
	SDL_AUDIODRIVER=dummy ./$(BUILD_DIR)/4d_game.app/Contents/MacOS/4d_game --headless --mute \
		--view-compare $(COMPARE_DIR) --fps 30
	for d in $(COMPARE_DIR)/*/; do \
		$(FFMPEG) -nostdin -loglevel error -y -framerate 30 -i $${d}frame_%06d.png -c:v libx264 -preset slow \
			-crf 18 -pix_fmt yuv420p -movflags +faststart $${d%/}.mp4 || exit 1; \
	done
