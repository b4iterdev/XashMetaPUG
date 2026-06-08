CXX ?= g++
SDK_ROOT ?= /Users/b4iterdev/MatchBot/MatchBot/include
BUILD_DIR ?= build
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
TARGET ?= $(BUILD_DIR)/xashmetapug_mm.dylib
else
TARGET ?= cstrike/addons/xashmetapug/dlls/xashmetapug_mm_arm64.so
endif

# game_shared needed for regamedll_api.h (via gamerules.h -> voice_gamemgr.h)
INCLUDES := \
	-I$(SDK_ROOT)/cssdk/common \
	-I$(SDK_ROOT)/cssdk/dlls \
	-I$(SDK_ROOT)/cssdk/engine \
	-I$(SDK_ROOT)/cssdk/game_shared \
	-I$(SDK_ROOT)/cssdk/pm_shared \
	-I$(SDK_ROOT)/cssdk/public \
	-I$(SDK_ROOT)/metamod

ifeq ($(UNAME_S),Darwin)
# macOS smoke-test: provide Linux-compat headers and defines
INCLUDES := -Iinclude/darwin-compat $(INCLUDES)
CXXFLAGS += -D__linux__ -D_vsnprintf=vsnprintf
endif

SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	mkdir -p $(BUILD_DIR) cstrike/addons/xashmetapug/dlls

$(TARGET): $(OBJECTS)
	$(CXX) -shared -o $@ $^

$(BUILD_DIR)/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
