PICO_PROG = build/stereoBoy_RP2350_FW.uf2
FILE_NAME = stereoBoy_RP2350_FW.uf2
GIT_FILES ?= .
MESSAGE ?= There was no given commit message
VENV_PATH ?= none
REPORTS_DIR := reports
SESSION_FILE := session_number
REPORT_PREFIX := report_
ACTIVE_SESSION := active_session

ifeq ($(OS),Windows_NT)
    OS_NAME := Windows
    CLEAN_COMMAND := cmake --build build --target clean
    GENERATOR := "MinGW Makefiles"
    RP_PATH := D:/
    COPY := @powershell -Command Copy-Item
	REMOVE_FILE := powershell rm
	ifeq ($(VENV_PATH), none)
		PYTHON_EXE = python
	else
		PYTHON_EXE = $(VENV_PATH)/Scripts/python
	endif
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        OS_NAME := Mac
        RP_PATH := /Volumes/RP2350/
    else
        OS_NAME := Linux
        RP_PATH := /media/RP2350/
    endif
    CLEAN_COMMAND := cmake --build build --target clean
    GENERATOR := "Unix Makefiles"
    COPY := cp
	REMOVE_FILE := rm
	ifeq ($(VENV_PATH), none)
		PYTHON_EXE = python
	else
		PYTHON_EXE = $(VENV_PATH)/bin/python
	endif
endif

# --- Targets ---
build: $(PICO_PROG)

$(PICO_PROG): src/main.pio src/main.c
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

#REPORT STUFF NOT ESSENTIAL, DON"T GET JUMBLED UP HERE


start_session:
	@$(PYTHON_EXE) start_session.py "$(MESSAGE)"
	
report:
	@echo "Running report script..."
	@echo "$(MESSAGE)"
	@git add $(GIT_FILES)
	@git commit -m "$(MESSAGE)"
	@git diff HEAD~1 HEAD > difference.txt
	@$(PYTHON_EXE) report.py "$(MESSAGE)"
	@$(REMOVE_FILE) difference.txt

end_session:
	@$(PYTHON_EXE) end_session.py "$(MESSAGE)"

help:
	@echo "Supported Commands"
	@echo " PI Build Commands"
	@echo "  build"
	@echo "  clean"
	@echo "  flash"
	@echo " Report Germination"
	@echo "  start_session"
	@echo "  report"
	@echo "  end_session"

.PHONY: clean flash start_session report end_session
.IGNORE: clear
