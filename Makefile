# SPDX-License-Identifier: GPL-3.0-only
APP_ID := dev.lumigtk.app
TARGET := lumigtk
PREFIX ?= /usr/local
CC ?= cc
SRCS := src/main.c src/common.c src/settings.c src/player.c src/importer.c
OBJS := $(SRCS:.c=.o)
PKGS := gtk4 gstreamer-1.0 gdk-pixbuf-2.0

CFLAGS += -std=gnu17 -O2 -Wall -Wextra -Wno-unused-parameter \
	$(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

all: $(TARGET)

$(TARGET): $(OBJS)
		$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJS): $(wildcard src/*.h)

%.o: %.c
		$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
		install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
		install -Dm644 data/$(APP_ID).desktop $(DESTDIR)$(PREFIX)/share/applications/$(APP_ID).desktop
		install -Dm644 data/$(APP_ID).svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/$(APP_ID).svg
		-gtk-update-icon-cache -q -t $(DESTDIR)$(PREFIX)/share/icons/hicolor 2>/dev/null

uninstall:
		rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
		rm -f $(DESTDIR)$(PREFIX)/share/applications/$(APP_ID).desktop
		rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/$(APP_ID).svg

clean:
		rm -f $(OBJS) $(TARGET)

.PHONY: all install uninstall clean
