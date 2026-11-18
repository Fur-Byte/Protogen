# Host-only harness of the production sketch.
.DEFAULT_GOAL := all
CXX := clang++
ROOT := ../..
SKETCH := $(ROOT)/firmware/PROT_OS7_MAX7219
STUBS := $(ROOT)/scripts/tests/stubs
ARDUINO_FFT_SRC ?= $(HOME)/Documents/Arduino/libraries/arduinoFFT/src
BUILD := $(ROOT)/build/twin
CORE := $(SKETCH)/src/BehaviorCore/BehaviorCore.cpp $(SKETCH)/src/BehaviorCore/AnimationV2.cpp $(SKETCH)/src/BehaviorCore/Timeline.cpp $(SKETCH)/src/AudioV2/AudioV2.cpp
CORE_INPUTS := $(CORE) $(wildcard $(SKETCH)/src/BehaviorCore/*.h $(SKETCH)/src/AudioV2/*.h) \
  $(SKETCH)/BlinkTiming.h $(SKETCH)/Configuration.h $(SKETCH)/Settings.h
SKETCH_SOURCES := $(SKETCH)/src/Max72xxPanel/Max72xxPanel.cpp $(CORE)
SKETCH_INPUTS := Makefile ../tests/sketch.h $(BUILD)/fft.o $(SKETCH_SOURCES) \
  $(wildcard $(SKETCH)/*.ino $(SKETCH)/*.h $(SKETCH)/src/*/*.h $(STUBS)/*.h)
PROTOGEN_HOST_SANITIZERS ?= address,undefined
MICROPHONE ?= -DMicrophone
FLAGS := $(EXTRA_FLAGS) -std=c++17 -g -O1 -Wall -Wextra -Werror -fsanitize=$(PROTOGEN_HOST_SANITIZERS) -fno-sanitize-recover=all \
  -DARDUINO=10819 -DARDUINO_TEENSY40 $(MICROPHONE) -DBoopSensor -DVisorSensor -DWirelessPaw \
  -I"$(STUBS)" -I"$(SKETCH)" -I"$(ARDUINO_FFT_SRC)"
# Rebuild whenever the effective feature/profile/toolchain identity changes,
# including A -> B -> A in one output directory. Ordinary make prerequisites
# don't track command-line variables and can otherwise reuse stale binaries.
COMPILER_PATH := $(shell command -v $(firstword $(CXX)))
COMPILER_ID := $(shell $(CXX) --version 2>/dev/null)
quote = '$(subst ','"'"',$(1))'
PROFILE := $(shell printf '%s\n' $(call quote,$(CXX)) $(call quote,$(COMPILER_PATH)) \
  $(call quote,$(COMPILER_ID)) $(call quote,$(FLAGS)) \
  $(call quote,$(ARDUINO_FFT_SRC)) $(call quote,$(TIMELINE_HEADER)) | shasum -a 256 | cut -d' ' -f1)
# Each target owns its profile. An intervening build of a different target
# cannot mark this target current. FORCE also works on make versions that
# compare timestamps at whole-second precision.
profile-dependency = $(if $(filter $(PROFILE),$(shell cat "$(BUILD)/$(1).profile" 2>/dev/null)),,FORCE)
record-profile = printf '%s\n' "$(PROFILE)" > "$@.profile"
.DELETE_ON_ERROR:
.PHONY: FORCE all
FORCE:
all: $(BUILD)/twin
$(BUILD):
	mkdir -p "$@"
$(BUILD)/fft.o: $(ARDUINO_FFT_SRC)/arduinoFFT.cpp $(wildcard $(ARDUINO_FFT_SRC)/*.h) $(STUBS)/Arduino.h ../host-fft.sh Makefile $(call profile-dependency,fft.o) | $(BUILD)
	bash -c 'source ../host-fft.sh; FLAGS=("$${@:3}"); compile_host_fft "$$1" "$$2"' _ "$(ARDUINO_FFT_SRC)" "$@" $(FLAGS)
	$(record-profile)
$(BUILD)/twin: twin.cpp $(SKETCH_INPUTS) $(call profile-dependency,twin)
	$(CXX) $(FLAGS) "$<" $(SKETCH_SOURCES) "$(BUILD)/fft.o" -o "$@"
	$(record-profile)

.PHONY: p3-tests
p3-tests: $(BUILD)/flight-recorder-unit $(BUILD)/flight-recorder-integration
$(BUILD)/flight-recorder-unit: ../tests/flight-recorder-unit.cpp $(SKETCH)/src/HelmetProtocol/FlightRecorder.h $(SKETCH)/FlightInstrumentation.h $(STUBS)/Arduino.h Makefile $(call profile-dependency,flight-recorder-unit) | $(BUILD)
	$(CXX) $(FLAGS) -pthread "$<" -o "$@"
	$(record-profile)
$(BUILD)/flight-recorder-integration: ../tests/flight-recorder-integration.cpp $(SKETCH_INPUTS) $(call profile-dependency,flight-recorder-integration)
	$(CXX) $(FLAGS) "$<" $(SKETCH_SOURCES) "$(BUILD)/fft.o" -o "$@"
	$(record-profile)

.PHONY: behavior-core-test
behavior-core-test: $(BUILD)/behavior-core-unit
	"$<"
$(BUILD)/behavior-core-unit: ../tests/behavior-core.cpp $(CORE_INPUTS) Makefile $(call profile-dependency,behavior-core-unit) | $(BUILD)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=$(PROTOGEN_HOST_SANITIZERS) \
	  -fno-sanitize-recover=all -I"$(SKETCH)/src/BehaviorCore" "$<" $(CORE) -o "$@"
	$(record-profile)

.PHONY: animation-v2-tests
animation-v2-tests: $(BUILD)/animation-v2-unit $(BUILD)/animation-v2-visual
$(BUILD)/animation-v2-unit: ../tests/animation-v2.cpp $(CORE_INPUTS) Makefile $(call profile-dependency,animation-v2-unit) | $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -DPROTOGEN_ANIMATION_V2=1 \
	  -fsanitize=$(PROTOGEN_HOST_SANITIZERS) -fno-sanitize-recover=all \
	  -I"$(SKETCH)/src/BehaviorCore" "$<" $(CORE) -o "$@"
	$(record-profile)
$(BUILD)/animation-v2-visual: ../tests/animation-v2-visual.cpp $(SKETCH_INPUTS) $(call profile-dependency,animation-v2-visual)
	$(CXX) $(FLAGS) "$<" $(SKETCH_SOURCES) "$(BUILD)/fft.o" -o "$@"
	$(record-profile)

.PHONY: audio-corpus
audio-corpus: $(BUILD)/audio-corpus
$(BUILD)/audio-corpus: ../tests/audio-corpus.cpp $(SKETCH_INPUTS) $(call profile-dependency,audio-corpus)
	$(CXX) $(FLAGS) "$<" $(SKETCH_SOURCES) "$(BUILD)/fft.o" -o "$@"
	$(record-profile)

.PHONY: audio-v2-unit
audio-v2-unit: $(BUILD)/audio-v2-unit
	"$<"
$(BUILD)/audio-v2-unit: ../tests/audio-v2-unit.cpp $(SKETCH)/src/AudioV2/AudioV2.cpp $(SKETCH)/src/AudioV2/AudioV2.h Makefile $(call profile-dependency,audio-v2-unit) | $(BUILD)
	$(CXX) $(FLAGS) -I"$(SKETCH)/src/AudioV2" "$<" "$(SKETCH)/src/AudioV2/AudioV2.cpp" -o "$@"
	$(record-profile)

.PHONY: audio-profile microphone-recovery
audio-profile: $(BUILD)/audio-profile
microphone-recovery: $(BUILD)/microphone-recovery
$(BUILD)/audio-profile: ../tests/audio-profile.cpp $(SKETCH_INPUTS) $(TIMELINE_HEADER) $(call profile-dependency,audio-profile)
	$(CXX) $(FLAGS) $(if $(TIMELINE_HEADER),-I"$(dir $(TIMELINE_HEADER))") "$<" $(SKETCH_SOURCES) "$(BUILD)/fft.o" -o "$@"
	$(record-profile)
$(BUILD)/microphone-recovery: ../tests/microphone-recovery.cpp $(SKETCH_INPUTS) $(call profile-dependency,microphone-recovery)
	$(CXX) $(FLAGS) "$<" $(SKETCH_SOURCES) "$(BUILD)/fft.o" -o "$@"
	$(record-profile)
