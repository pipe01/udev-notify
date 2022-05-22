CFLAGS := $(shell pkg-config --cflags --libs libudev)

watcher: watcher.c
	gcc -o $@ $^ $(CFLAGS)

run: watcher
	./watcher