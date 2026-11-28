# CONTRXL 25 - MPC Sample edition
#   make sim      desktop simulator (this computer)
#   make mpc      the MPC Sample binary (Linux arm64, static)
TA      = third_party/tinyalsa
SRC     = src/main.cpp src/app.cpp src/app_draw.cpp src/engine.cpp src/dsp.cpp src/model.cpp src/json.cpp src/fileio.cpp src/gfx.cpp src/assets.cpp
TASRC   = $(TA)/src/pcm.c $(TA)/src/pcm_hw.c $(TA)/src/mixer.c $(TA)/src/mixer_hw.c $(TA)/src/limits.c $(TA)/src/snd_card_plugin.c
CXXFLAGS= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc -pthread
sim: build/contrxl25-sim
mpc: build/contrxl25
build/contrxl25-sim: $(SRC) src/*.h
	mkdir -p build && g++ $(CXXFLAGS) -g -o $@ $(SRC) src/hw_stub.cpp
build/contrxl25: $(SRC) src/hw_mpc.cpp src/*.h
	mkdir -p build/ta
	for f in $(TASRC); do aarch64-linux-gnu-gcc -O2 -I$(TA)/include -c $$f -o build/ta/$$(basename $$f .c).o || exit 1; done
	aarch64-linux-gnu-g++ $(CXXFLAGS) -mcpu=cortex-a55 -I$(TA)/include -static -o $@ $(SRC) src/hw_mpc.cpp build/ta/*.o
	aarch64-linux-gnu-strip $@
assets:
	python3 tools/gen_assets.py assets/Illah.rgb565
package: mpc
	cp build/contrxl25 mod/contrxl25
	rm -rf dist && mkdir -p dist/mods/CONTRXL25
	cp mod/autorun.sh mod/contrxl25 mod/cover.rgb565 mod/illah.txt mod/watch.txt docs/README.txt dist/mods/CONTRXL25/
	cd dist && zip -qr CONTRXL25-MPC-Sample-$$(../build/contrxl25-sim --version | awk '{print $$NF}').zip mods
