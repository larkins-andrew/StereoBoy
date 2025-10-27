PICO_PROG = build/stereoBoy_RP2350_FW.uf2
FILE_NAME = stereoBoy_RP2350_FW.uf2
GIT_FILES ?= .
MESSAGE ?= "no commit message"
VENV_PATH ?= none


ifeq ($(VENV_PATH), none)
	ACTIVATE = echo "no venv, running directly..."
else
	ACTIVATE = @powershell $(VENV_PATH)/Scripts/activate
endif

ifeq ($(OS),Windows_NT)
    OS_NAME := Windows
    CLEAN_COMMAND := rmdir /S /Q build
    GENERATOR := "MinGW Makefiles"
    RP_PATH := D:/
    COPY := @powershell -Command Copy-Item
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        OS_NAME := Mac
        RP_PATH := /Volumes/RP2350/
    else
        OS_NAME := Linux
        RP_PATH := /media/RP2350/
    endif
    CLEAN_COMMAND := rm -rf build/
    GENERATOR := "Unix Makefiles"
    COPY := cp
endif

# --- Targets ---
build: $(PICO_PROG)

$(PICO_PROG):
	cmake -B build -G $(GENERATOR) -DPICO_SDK_PATH=./pico-sdk -DPICO_BOARD=pico2
	cmake --build build

clean:
	@$(CLEAN_COMMAND)

flash:
	@echo Flashing to $(OS_NAME) drive...
ifeq ($(OS_NAME),Windows)
	$(COPY) "$(PICO_PROG)" "$(RP_PATH)$(FILE_NAME)"
else
	@$(COPY) "$(PICO_PROG)" "$(RP_PATH)$(FILE_NAME)"
endif

report:
	@echo "Running report script..."
	@git add $(GIT_FILES)
	@git commit -m $(MESSAGE)
	@git diff HEAD~1 HEAD > difference.txt
	$(ACTIVATE)
	@python report.py "$(MESSAGE)"
	@rm difference.txt

.PHONY: clean flash
.IGNORE: clear
