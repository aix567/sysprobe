CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude
LDFLAGS ?=
BUILD := build
SOURCES := src/main.c src/collect.c src/output.c src/tui.c
.PHONY: all test clean sanitize install
all: $(BUILD)/sysprobe
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/sysprobe: $(SOURCES) include/sysprobe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@
$(BUILD)/unit: tests/unit.c src/collect.c include/sysprobe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/unit.c src/collect.c $(LDFLAGS) -o $@
test: $(BUILD)/sysprobe $(BUILD)/unit
	./$(BUILD)/unit
	python3 tests/integration.py ./$(BUILD)/sysprobe
	python3 tests/tui_smoke.py ./$(BUILD)/sysprobe
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 -g -O1 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' all
clean:
	rm -rf $(BUILD)
install: $(BUILD)/sysprobe
	install -Dm755 $(BUILD)/sysprobe $(DESTDIR)$(PREFIX)/bin/sysprobe
PREFIX ?= /usr/local
