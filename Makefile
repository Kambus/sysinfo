CC = gcc
CFLAGS = -Wall -fPIC

all: sysinfo.so


sysinfo.so: sysinfo.o
	$(CC) $(CFLAGS) -shared -o $@ $^

sysinfo.o: sysinfo.c
	$(CC) $(CFLAGS) -c -o $@ $^

clean:
	rm sysinfo.o sysinfo.so

install:
	cp -f sysinfo.so ~/.weechat/plugins/
