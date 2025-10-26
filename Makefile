RP_PATH = /Volumes/RP2350/

build: main.c
	cmake -B build
	cmake --build build

clean:
	rm -rf build/

flash: build/stereoBoy_RP2350_FW.uf2
	cp build/stereoBoy_RP2350_FW.uf2 $(RP_PATH)/. 
