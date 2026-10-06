CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS += -Ithird_party -Isrc -Itools

LIB_SRC = src/audio_reader.cpp src/biphase.cpp src/ltc_frame.cpp src/segments.cpp src/conform.cpp

all: ltc_conform239 ltc_gen test_decoder test_conform

ltc_conform239: $(LIB_SRC) src/main.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $^ -o $@

ltc_gen: tools/ltc_gen.cpp tools/ltc_encoder.h src/ltc_frame.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) tools/ltc_gen.cpp src/ltc_frame.cpp -o $@

test_decoder: tests/test_decoder.cpp $(LIB_SRC) tools/ltc_encoder.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) tests/test_decoder.cpp $(LIB_SRC) -o $@

test_conform: tests/test_conform.cpp $(LIB_SRC) tools/ltc_encoder.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) tests/test_conform.cpp $(LIB_SRC) -o $@

test: test_decoder test_conform
	./test_decoder
	./test_conform

example: ltc_conform239 ltc_gen
	./ltc_gen --out example.wav --start 01:00:00:00 --frames 250 --lead-silence 4800 --channels 2 --channel 1
	./ltc_conform239 example.wav --json example.json
	@echo "wrote example.json"

# Reproducible dual-recorder material: two stereo files, LTC on opposite
# channels, distinct program tones, recorder B 1% fast.
dual: ltc_conform239 ltc_gen
	./ltc_gen --out machine_a.wav --start 10:00:00:00 --frames 250 --lead-silence 4800 --channels 2 --channel 1 --program-freq 440
	./ltc_gen --out machine_b.wav --start 10:00:00:00 --frames 250 --lead-silence 9600 --channels 2 --channel 0 --program-freq 550 --speed 1.01
	./ltc_conform239 conform --a machine_a.wav --channel-a 1 --segment-a 0 --b machine_b.wav --channel-b 0 --segment-b 0 --start 10:00:00:05 --frames 100 --out dual_out.wav --json dual_handoff.json
	@echo "wrote dual_out.wav and dual_handoff.json"

clean:
	rm -f ltc_conform239 ltc_gen test_decoder test_conform example.wav example.json test_stereo.wav machine_a.wav machine_b.wav dual_out.wav dual_out.wav.tmp dual_handoff.json

.PHONY: all test example dual clean
