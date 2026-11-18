# Include after scripts/twin/Makefile to reuse the pinned FFT and sketch stubs.
.PHONY: supervision-integration
supervision-integration: $(BUILD)/supervision-integration
$(BUILD)/supervision-integration: ../tests/supervision-integration.cpp $(SKETCH_INPUTS) ../supervision.mk $(call profile-dependency,supervision-integration)
	$(CXX) $(FLAGS) "$<" $(SKETCH_SOURCES) "$(BUILD)/fft.o" -o "$@"
	$(record-profile)
