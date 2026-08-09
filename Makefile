CXX ?= c++
CPPFLAGS += $(shell pkg-config --cflags libsystemd)
CXXFLAGS ?= -O2 -pipe
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
LDLIBS += $(shell pkg-config --libs libsystemd)
BINARY ?= onekvm-nanokvm-oled
PREFIX ?= /usr/local
DESTDIR ?=
VERSION ?= 0.0.0-dev
ARCH ?= unknown
SOURCES := src/config.cpp src/controller.cpp src/json.cpp src/renderer.cpp \
	src/runtime.cpp src/status.cpp
HEADERS := $(wildcard src/*.hpp)

.PHONY: all web test install clean

all: $(BINARY)

web:
	npm run build:web

$(BINARY): src/main.cpp $(SOURCES) $(HEADERS)
	mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ src/main.cpp $(SOURCES) $(LDLIBS)

test: web tests/test_main
	./tests/test_main
	node --check web/oled/oled.js
	node tests/test_web.mjs

tests/test_main: tests/test_main.cpp $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ tests/test_main.cpp $(SOURCES) $(LDLIBS)

install: $(BINARY)
	test -f web/dist/oled-page.js
	install -Dm0755 $(BINARY) $(DESTDIR)$(PREFIX)/bin/onekvm-nanokvm-oled
	install -Dm0644 web/dist/oled-page.js $(DESTDIR)$(PREFIX)/web/oled-page.js
	install -Dm0644 web/oled/font-cjk-12.js $(DESTDIR)$(PREFIX)/web/font-cjk-12.js
	install -Dm0644 fonts/LICENSE-OFL-1.1.txt $(DESTDIR)$(PREFIX)/fonts/LICENSE-OFL-1.1.txt
	install -d $(DESTDIR)$(PREFIX)
	sed -e 's/@ARCH@/$(ARCH)/g' -e 's/@VERSION@/$(VERSION)/g' \
		manifest.json.in >$(DESTDIR)$(PREFIX)/manifest.json
	chmod 0644 $(DESTDIR)$(PREFIX)/manifest.json

clean:
	$(RM) $(BINARY) tests/test_main
	$(RM) -r web/dist
