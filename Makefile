RP_PATH = /Volumes/RP2350
PICO_PROG = build/stereoBoy_RP2350_FW.uf2

findos:
	@-ifeq (Windows_NT, $(env:os))
	@-OS = Windows
	@-endif
	@-ifeq ($(uname), Darwin)
	@-OS = Mac
	@-endif

build: $(PICO_PROG)
	cmake -B build
	cmake --build build

clean: findos
	echo $(OS)
	ifeq ($(OS), Windows)
		rm -r -Force build/
	endif
	ifeq ($(OS), Mac)
		rm -rf build/
	endif

flash:
	cp $(PICO_PROG) $(RP_PATH)/. 


.PHONY: clean flash

.IGNORE: clear

