# Host-only harness of the production sketch.
CXX := clang++
ROOT := ../..
SKETCH := $(ROOT)/firmware/PROT_OS7_MAX7219
ARDUINO_FFT_SRC ?= $(HOME)/Documents/Arduino/libraries/arduinoFFT/src
BUILD := $(ROOT)/build/twin
PROTOGEN_HOST_SANITIZERS ?= address,undefined
FLAGS := -std=c++17 -g -O1 -Wall -Wextra -Werror -fsanitize=$(PROTOGEN_HOST_SANITIZERS) -fno-sanitize-recover=all \
  -DARDUINO=10819 -DARDUINO_TEENSY40 -DMicrophone -DBoopSensor -DVisorSensor -DWirelessPaw \
  -I"$(ROOT)/scripts/tests/stubs" -I"$(SKETCH)" -I"$(ARDUINO_FFT_SRC)"
.PHONY: all
all: $(BUILD)/twin
$(BUILD):
	mkdir -p "$@"
$(BUILD)/fft.o: $(ARDUINO_FFT_SRC)/arduinoFFT.cpp Makefile | $(BUILD)
	bash -c 'source ../host-fft.sh; FLAGS=("$${@:3}"); compile_host_fft "$$1" "$$2"' _ "$(ARDUINO_FFT_SRC)" "$@" $(FLAGS)
$(BUILD)/twin: twin.cpp Makefile $(BUILD)/fft.o $(wildcard $(SKETCH)/*.h $(SKETCH)/*.ino $(SKETCH)/src/*/*.h $(ROOT)/scripts/tests/stubs/*.h) ../tests/sketch.h $(SKETCH)/src/Max72xxPanel/Max72xxPanel.cpp
	$(CXX) $(FLAGS) twin.cpp "$(SKETCH)/src/Max72xxPanel/Max72xxPanel.cpp" "$(BUILD)/fft.o" -o "$@"
