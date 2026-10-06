CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS += -Ithird_party -Isrc -Itools

LIB_SRC = src/audio_reader.cpp src/biphase.cpp src/ltc_frame.cpp src/segments.cpp

all: ltc_conform239 ltc_gen test_decoder

ltc_conform239: $(LIB_SRC) src/main.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $^ -o $@

ltc_gen: tools/ltc_gen.cpp tools/ltc_encoder.h src/ltc_frame.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) tools/ltc_gen.cpp src/ltc_frame.cpp -o $@

test_decoder: tests/test_decoder.cpp $(LIB_SRC) tools/ltc_encoder.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) tests/test_decoder.cpp $(LIB_SRC) -o $@

test: test_decoder
	./test_decoder

example: ltc_conform239 ltc_gen
	./ltc_gen --out example.wav --start 01:00:00:00 --frames 250 --lead-silence 4800 --channels 2 --channel 1
	./ltc_conform239 example.wav --json example.json
	@echo "wrote example.json"

clean:
	rm -f ltc_conform239 ltc_gen test_decoder example.wav example.json test_stereo.wav

.PHONY: all test example clean
