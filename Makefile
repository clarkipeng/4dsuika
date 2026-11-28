# Makefile for Suika4D

BUILD_DIR = build
CMAKE = cmake
NICE = nice -n 10
JOBS = 4

.PHONY: all build run clean package video gameplay turntable view-compare site-clips web web-serve

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

# Browser build (docs/web.md): build-web/ holds index.html, .js, .wasm and
# .data, the files itch takes as the HTML5 build. Uses tools/emsdk when present
# (gitignored, `git clone https://github.com/emscripten-core/emsdk tools/emsdk`
# then `./emsdk install 3.1.74 && ./emsdk activate 3.1.74`), else emcmake on PATH.
EMCMAKE = $(or $(wildcard tools/emsdk/upstream/emscripten/emcmake),emcmake)

web:
	$(NICE) $(EMCMAKE) $(CMAKE) -S . -B build-web -DCMAKE_BUILD_TYPE=Release
	$(NICE) $(CMAKE) --build build-web -j $(JOBS)
	cd build-web && $(CMAKE) -E tar cf web.zip --format=zip index.html index.js index.wasm index.data

web-serve: web
	cd build-web && python3 -m http.server 8642

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

# Site hover clips: one scripted run on site-c.path, three moments from it, encoded for the site (see docs/recording.md).
# A clip is name:first-frame:frames in the run's consecutively numbered capture; range edges sit mid-frame.
SITE_DIR ?= recordings/site
SITE_RANGES = 156.483-161.983,221.483-226.483,255.583-260.583
SITE_CLIPS = suika-03:0:165 suika-02:165:150 suika-01:315:150

site-clips:
	rm -rf $(SITE_DIR)/frames && mkdir -p $(SITE_DIR)
	SDL_AUDIODRIVER=dummy ./$(BUILD_DIR)/4d_game.app/Contents/MacOS/4d_game --headless --mute --record $(SITE_DIR)/frames \
		--camera-path docs/recording/paths/site-c.path --play 0.8 --warmup 10 --fps 30 --size 2400x1600 --lane-snap \
		--no-ui --bg-soft 0 --seconds 261 --capture-ranges $(SITE_RANGES) --log-events 2> $(SITE_DIR)/events.log
	for c in $(SITE_CLIPS); do set -- $$(echo $$c | tr : ' '); \
		$(FFMPEG) -nostdin -loglevel error -y -framerate 30 -start_number $$2 -i $(SITE_DIR)/frames/frame_%06d.png -frames:v $$3 \
			-vf scale=1200:800:flags=lanczos,format=yuv420p -an -c:v libx264 -profile:v high -preset slow -crf 23 \
			-maxrate 2M -bufsize 4M -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv \
			-movflags +faststart $(SITE_DIR)/$$1.mp4 || exit 1; \
	done
	$(FFMPEG) -nostdin -loglevel error -y -i $(SITE_DIR)/suika-01.mp4 -frames:v 1 -q:v 2 $(SITE_DIR)/suika-poster.jpg
