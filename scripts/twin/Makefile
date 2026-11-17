# Host-only harness of the production sketch.
CXX := clang++
ROOT := ../..
SKETCH := $(ROOT)/firmware/PROT_OS7_MAX7219
ARDUINO_FFT_SRC ?= $(HOME)/Documents/Arduino/libraries/arduinoFFT/src
BUILD := $(ROOT)/build/twin
CORE := $(SKETCH)/src/BehaviorCore/BehaviorCore.cpp $(SKETCH)/src/BehaviorCore/AnimationV2.cpp $(SKETCH)/src/BehaviorCore/Timeline.cpp $(SKETCH)/src/AudioV2/AudioV2.cpp
PROTOGEN_HOST_SANITIZERS ?= address,undefined
FLAGS := $(EXTRA_FLAGS) -std=c++17 -g -O1 -Wall -Wextra -Werror -fsanitize=$(PROTOGEN_HOST_SANITIZERS) -fno-sanitize-recover=all \
  -DARDUINO=10819 -DARDUINO_TEENSY40 -DMicrophone -DBoopSensor -DVisorSensor -DWirelessPaw \
  -I"$(ROOT)/scripts/tests/stubs" -I"$(SKETCH)" -I"$(ARDUINO_FFT_SRC)"
.PHONY: all
all: $(BUILD)/twin
$(BUILD):
	mkdir -p "$@"
$(BUILD)/fft.o: $(ARDUINO_FFT_SRC)/arduinoFFT.cpp Makefile | $(BUILD)
	bash -c 'source ../host-fft.sh; FLAGS=("$${@:3}"); compile_host_fft "$$1" "$$2"' _ "$(ARDUINO_FFT_SRC)" "$@" $(FLAGS)
$(BUILD)/twin: twin.cpp Makefile $(BUILD)/fft.o $(wildcard $(SKETCH)/*.h $(SKETCH)/*.ino $(SKETCH)/src/*/*.h $(ROOT)/scripts/tests/stubs/*.h) ../tests/sketch.h $(SKETCH)/src/Max72xxPanel/Max72xxPanel.cpp $(CORE)
	$(CXX) $(FLAGS) twin.cpp "$(SKETCH)/src/Max72xxPanel/Max72xxPanel.cpp" $(CORE) "$(BUILD)/fft.o" -o "$@"

.PHONY: p3-tests
p3-tests: $(BUILD)/flight-recorder-unit $(BUILD)/flight-recorder-integration
$(BUILD)/flight-recorder-unit: ../tests/flight-recorder-unit.cpp $(SKETCH)/src/HelmetProtocol/FlightRecorder.h $(SKETCH)/FlightInstrumentation.h Makefile | $(BUILD)
	$(CXX) $(FLAGS) -pthread "$<" -o "$@"
$(BUILD)/flight-recorder-integration: ../tests/flight-recorder-integration.cpp $(BUILD)/fft.o $(wildcard $(SKETCH)/*.ino $(SKETCH)/*.h $(SKETCH)/src/*/*.h $(ROOT)/scripts/tests/stubs/*.h) ../tests/sketch.h $(CORE) Makefile
	$(CXX) $(FLAGS) "$<" "$(SKETCH)/src/Max72xxPanel/Max72xxPanel.cpp" $(CORE) "$(BUILD)/fft.o" -o "$@"

.PHONY: behavior-core-test
behavior-core-test: $(BUILD)/behavior-core-unit
	"$<"
$(BUILD)/behavior-core-unit: ../tests/behavior-core.cpp $(CORE) $(SKETCH)/src/BehaviorCore/BehaviorCore.h $(SKETCH)/BlinkTiming.h $(SKETCH)/Configuration.h $(SKETCH)/Settings.h Makefile | $(BUILD)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=$(PROTOGEN_HOST_SANITIZERS) \
	  -fno-sanitize-recover=all -I"$(SKETCH)/src/BehaviorCore" "$<" $(CORE) -o "$@"

.PHONY: animation-v2-tests
animation-v2-tests: $(BUILD)/animation-v2-unit $(BUILD)/animation-v2-visual
$(BUILD)/animation-v2-unit: ../tests/animation-v2.cpp $(CORE) $(wildcard $(SKETCH)/src/BehaviorCore/*.h) Makefile | $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -DPROTOGEN_ANIMATION_V2=1 \
	  -fsanitize=$(PROTOGEN_HOST_SANITIZERS) -fno-sanitize-recover=all \
	  -I"$(SKETCH)/src/BehaviorCore" "$<" $(CORE) -o "$@"
$(BUILD)/animation-v2-visual: ../tests/animation-v2-visual.cpp $(BUILD)/fft.o $(wildcard $(SKETCH)/*.ino $(SKETCH)/*.h $(SKETCH)/src/*/*.h $(ROOT)/scripts/tests/stubs/*.h) ../tests/sketch.h $(CORE) Makefile
	$(CXX) $(FLAGS) "$<" "$(SKETCH)/src/Max72xxPanel/Max72xxPanel.cpp" $(CORE) "$(BUILD)/fft.o" -o "$@"

.PHONY: audio-corpus
audio-corpus: $(BUILD)/audio-corpus
$(BUILD)/audio-corpus: ../tests/audio-corpus.cpp $(BUILD)/fft.o $(wildcard $(SKETCH)/*.ino $(SKETCH)/*.h $(SKETCH)/src/*/*.h) ../tests/sketch.h $(CORE) Makefile
	$(CXX) $(FLAGS) "$<" "$(SKETCH)/src/Max72xxPanel/Max72xxPanel.cpp" $(CORE) "$(BUILD)/fft.o" -o "$@"

.PHONY: audio-v2-unit
audio-v2-unit: $(BUILD)/audio-v2-unit
	"$<"
$(BUILD)/audio-v2-unit: ../tests/audio-v2-unit.cpp $(SKETCH)/src/AudioV2/AudioV2.cpp $(SKETCH)/src/AudioV2/AudioV2.h Makefile | $(BUILD)
	$(CXX) $(FLAGS) -I"$(SKETCH)/src/AudioV2" "$<" "$(SKETCH)/src/AudioV2/AudioV2.cpp" -o "$@"
