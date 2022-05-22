CFLAGS := $(shell pkg-config --cflags --libs libudev libnotify libcanberra)
EXE_NAME := udev-notify

all: $(EXE_NAME)
.PHONY: all run install

$(EXE_NAME): watcher.c
	gcc -o $@ $^ $(CFLAGS)

run: $(EXE_NAME)
	./$(EXE_NAME)

install: $(EXE_NAME)
	install $(EXE_NAME) /usr/local/bin
