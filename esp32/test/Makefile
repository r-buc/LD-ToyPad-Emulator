# Host side unit tests for the hardware independent parts of the ESP32-S3 sketch.
# Run from the repository root: make -C esp32/test
# Requires g++ and git (Unity is cloned into build/ on first use).

SKETCH := ../LDToyPadEmulator
BUILD := build
UNITY := $(BUILD)/unity
UNITY_VERSION := v2.6.0
CXX ?= g++
CXXFLAGS := -std=gnu++17 -Wall -Wextra -Werror -O1 -I$(SKETCH) -I$(UNITY)/src

PROTOCOL_SRC := $(SKETCH)/ToyPadProtocol.cpp $(SKETCH)/ToyPadEmulator.cpp
STORE_SRC := $(PROTOCOL_SRC) $(SKETCH)/TagStore.cpp $(SKETCH)/Catalog.cpp
JSON_SRC := $(SKETCH)/JsonFields.cpp
TESTS := $(BUILD)/test_protocol $(BUILD)/test_store $(BUILD)/test_json_fields

.PHONY: test clean

test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$$t; done

$(UNITY)/src/unity.c:
	git -c advice.detachedHead=false clone --quiet --depth 1 --branch $(UNITY_VERSION) https://github.com/ThrowTheSwitch/Unity.git $(UNITY)

$(BUILD)/test_protocol: test_protocol.cpp $(PROTOCOL_SRC) $(UNITY)/src/unity.c
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/test_store: test_store.cpp $(STORE_SRC) $(UNITY)/src/unity.c
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/test_json_fields: test_json_fields.cpp $(JSON_SRC) $(UNITY)/src/unity.c
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -rf $(BUILD)
