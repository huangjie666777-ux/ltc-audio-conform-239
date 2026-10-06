CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS += -Ithird_party -Isrc -Itools

LIB_SRC = src/audio_reader.cpp src/biphase.cpp src/ltc_frame.cpp src/segments.cpp

all: ltc_conform239 ltc_conform239_sync ltc_gen test_decoder test_sync

ltc_conform239: $(LIB_SRC) src/main.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $^ -o $@

ltc_conform239_sync: $(LIB_SRC) src/conform.cpp src/sync_main.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $^ -o $@

ltc_gen: tools/ltc_gen.cpp tools/ltc_encoder.h src/ltc_frame.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) tools/ltc_gen.cpp src/ltc_frame.cpp -o $@

test_decoder: tests/test_decoder.cpp $(LIB_SRC) tools/ltc_encoder.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) tests/test_decoder.cpp $(LIB_SRC) -o $@

test_sync: tests/test_sync.cpp $(LIB_SRC) src/conform.cpp tools/ltc_encoder.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) tests/test_sync.cpp $(LIB_SRC) src/conform.cpp -o $@

test: test_decoder test_sync
	./test_decoder
	./test_sync

example: ltc_conform239 ltc_gen
	./ltc_gen --out example.wav --start 01:00:00:00 --frames 250 --lead-silence 4800 --channels 2 --channel 1
	./ltc_conform239 example.wav --json example.json
	@echo "wrote example.json"

sync-example: ltc_conform239_sync ltc_gen
	./ltc_gen --out sync_a.wav --start 01:00:00:00 --frames 300 --lead-silence 4800 --tail-silence 4800 --channels 2 --channel 1 --program-tone 440
	./ltc_gen --out sync_b.wav --start 01:00:00:00 --frames 300 --lead-silence 2400 --speed 1.01 --channels 2 --channel 0 --program-tone 550
	./ltc_conform239_sync --a sync_a.wav --a-channel 1 --a-segment 0 --b sync_b.wav --b-channel 0 --b-segment 0 --start 01:00:00:10 --frames 200 --out sync_out.wav --json sync_out.json
	@echo "wrote sync_out.wav and sync_out.json"

clean:
	rm -f ltc_conform239 ltc_conform239_sync ltc_gen test_decoder test_sync example.wav example.json test_stereo.wav sync_a.wav sync_b.wav sync_out.wav sync_out.wav.part sync_out.json

.PHONY: all test example sync-example clean
