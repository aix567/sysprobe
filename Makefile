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

# Desktop application (Fedora/Ubuntu): requires python3-tkinter at runtime.
.PHONY: install-desktop uninstall-desktop desktop-check
desktop-check:
	python3 -m py_compile gui/sysprobe_gui.py
install-desktop: all desktop-check
	install -Dm755 build/sysprobe $(DESTDIR)/usr/local/bin/sysprobe
	install -Dm755 gui/sysprobe_gui.py $(DESTDIR)/usr/local/share/sysprobe/gui/sysprobe_gui.py
	install -Dm755 packaging/sysprobe-gui $(DESTDIR)/usr/local/bin/sysprobe-gui
	install -Dm644 assets/io.github.aix567.SysProbe.desktop $(DESTDIR)/usr/local/share/applications/io.github.aix567.SysProbe.desktop
	install -Dm644 assets/io.github.aix567.SysProbe.svg $(DESTDIR)/usr/local/share/icons/hicolor/scalable/apps/io.github.aix567.SysProbe.svg
uninstall-desktop:
	rm -f $(DESTDIR)/usr/local/bin/sysprobe-gui $(DESTDIR)/usr/local/bin/sysprobe
	rm -f $(DESTDIR)/usr/local/share/sysprobe/gui/sysprobe_gui.py
	rm -f $(DESTDIR)/usr/local/share/applications/io.github.aix567.SysProbe.desktop
	rm -f $(DESTDIR)/usr/local/share/icons/hicolor/scalable/apps/io.github.aix567.SysProbe.svg
.PHONY: gui-test
gui-test: all desktop-check
	xvfb-run -a python3 tests/gui_smoke.py
