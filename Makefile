.SUFFIXES: .c .o .so
.PHONY: clean install

CC = gcc
CFLAGS = -Wall -fPIC

.o.so:
	$(CC) $(CFLAGS) -shared -o $@ $<

all: sysinfo.so

sysinfo.so: sysinfo.o

sysinfo.o: sysinfo.c

clean:
	rm sysinfo.o sysinfo.so

install:
	cp -f sysinfo.so ~/.weechat/plugins/
