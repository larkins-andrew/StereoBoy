PICO_PROG = build/stereoBoy_RP2350_FW.uf2
FILE_NAME = stereoBoy_RP2350_FW.uf2

ifeq ($(OS),Windows_NT)
    OS_NAME := Windows
	CLEAN_COMMAND := rmdir /S /Q build
	GENERATOR := "MinGW Makefiles"
	RP_PATH := D:/
	COPY := copy
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        OS_NAME := Mac
		RP_PATH := /Volumes/RP2350
    else
        OS_NAME := Linux
		RP_PATH := /media/RP2350
    endif
	CLEAN_COMMAND := rm -rf build/
	GENERATOR := "Unix Makefiles"
	COPY := cp
endif

build: $(PICO_PROG)

$(PICO_PROG):
	cmake -B build -G $(GENERATOR) -DPICO_SDK_PATH=./pico-sdk -DPICO_BOARD=pico2
	cmake --build build

clean: 
	@$(CLEAN_COMMAND)

flash:
	echo $(COPY) $(PICO_PROG) $(RP_PATH)$(FILE_NAME)
	@$(COPY) $(PICO_PROG) $(RP_PATH)$(FILE_NAME)

.PHONY: clean flash

.IGNORE: clear

