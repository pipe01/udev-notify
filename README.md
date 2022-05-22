# udev-notify
Shows a notification and plays a sound whenever a USB device is plugged in or unplugged.

## Requirements

* libudev
* libnotify
* libcanberra

On Debian-based systems these libraries can be installed with the following command:

```bash
sudo apt install libudev-dev libnotify-dev libcanberra-dev
```

## Installing

Clone this repo, then run:

```bash
make
sudo make install
