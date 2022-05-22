CFLAGS := $(shell pkg-config --cflags --libs libudev libnotify libcanberra)

watcher: watcher.c
	gcc -o $@ $^ $(CFLAGS)

run: watcher
	./watcher