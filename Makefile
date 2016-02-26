PROGS	= mpdstate
default	: $(PROGS)

VERSION:= $(shell git describe --always --tags --dirty)
PREFIX	= /usr/local
CFLAGS	= -Wall

-include config.mk

CPPFLAGS+= -DVERSION=\"$(VERSION)\"

.PHONY: clean install

clean:
	rm -rf $(PROGS) *.o

install: $(PROGS)
	@[ -d $(DESTDIR)$(PREFIX)/bin ] || install -v -d $(DESTDIR)$(PREFIX)/bin
	@install -v $(PROGS) $(DESTDIR)$(PREFIX)/bin

