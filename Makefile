BUILD_DIR ?= build
CMAKE ?= cmake

.PHONY: all configure build test install clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug

build: configure
	$(CMAKE) --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

install: build
	$(CMAKE) --install $(BUILD_DIR)

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)
