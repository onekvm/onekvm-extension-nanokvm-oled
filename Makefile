CXX ?= c++
CPPFLAGS += $(shell pkg-config --cflags libsystemd)
CXXFLAGS ?= -O2 -pipe
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
LDLIBS += $(shell pkg-config --libs libsystemd)
SOURCES := src/config.cpp src/controller.cpp src/json.cpp src/renderer.cpp \
	src/runtime.cpp src/status.cpp
HEADERS := $(wildcard src/*.hpp)

.PHONY: all web test clean

all: onekvm-nanokvm-oled

web:
	npm run build:web

onekvm-nanokvm-oled: src/main.cpp $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ src/main.cpp $(SOURCES) $(LDLIBS)

test: web tests/test_main
	./tests/test_main
	node --check web/oled/oled.js
	node tests/test_web.mjs

tests/test_main: tests/test_main.cpp $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ tests/test_main.cpp $(SOURCES) $(LDLIBS)

clean:
	$(RM) onekvm-nanokvm-oled tests/test_main
	$(RM) -r web/dist
