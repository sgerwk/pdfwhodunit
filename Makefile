PROGS=pdftrace.so pdfwhodunit

CC=gcc
CFLAGS+=-g -Wall -Wextra -Wformat -Wformat-security

CFLAGS_GLIB=${shell pkg-config --cflags poppler-glib}
LDLIBS_GLIB=${shell pkg-config --libs poppler-glib --libs gio-2.0}

CFLAGS_NCURSES=${shell pkg-config --libs ncurses}
LDLIBS_NCURSES=${shell pkg-config --libs ncurses || echo '' -lncurses -ltinfo}

CFLAGS_X11=${shell pkg-config --libs x11}
LDLIBS_X11=${shell pkg-config --libs x11}

pdftrace.so: CFLAGS+=-fPIC

pdfwhodunit.o: CFLAGS+=$(CFLAGS_GLIB)
pdfwhodunit: LDLIBS+=$(LDLIBS_GLIB)
pdfwhodunit: LDLIBS+=-ldl

all: $(PROGS)

%.so: %.o
	ld -o $@ -ldl -shared $<

clean:
	rm -f $(PROGS) *.o

