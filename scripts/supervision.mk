# Include after scripts/twin/Makefile to reuse the pinned FFT and sketch stubs.
.PHONY: supervision-integration
supervision-integration: $(BUILD)/supervision-integration
$(BUILD)/supervision-integration: ../tests/supervision-integration.cpp $(AUDIO_TEST_SOURCES) ../supervision.mk $(call profile-dependency,supervision-integration)
	$(CXX) $(FLAGS) "$<" "$(SKETCH)/src/Max72xxPanel/Max72xxPanel.cpp" $(CORE) "$(BUILD)/fft.o" -o "$@"
	$(record-profile)
