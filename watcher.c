#include <libudev.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <malloc.h>

typedef struct Device
{
    char *path;
    char *name;
    struct Device *next, *prev;
} Device;

Device *first_dev, *last_dev;

void add_device(const char* path, const char* name)
{
    Device *dev = malloc(sizeof(Device));
    dev->name = strdup(name);
    dev->path = strdup(path);
    dev->next = 0;
    dev->prev = 0;

    if (!first_dev)
    {
        first_dev = dev;
        last_dev = dev;
    }
    else
    {
        last_dev->next = dev;
        dev->prev = last_dev;
        last_dev = dev;
    }
}

void remove_device(Device *dev)
{
    if (dev->prev)
        dev->prev->next = dev->next;
    if (dev->next)
        dev->next->prev = dev->prev;

    if (dev == first_dev)
        first_dev = dev->next;
    if (dev == last_dev)
        last_dev = dev->prev;

    free(dev->name);
    free(dev->path);
    free(dev);
}

Device* find_device_path(const char *path)
{
    if (!first_dev)
        return 0;

    Device *dev = first_dev;
    do
    {
        if (strcmp(dev->path, path) == 0)
            return dev;

        dev = dev->next;
    } while (dev);

    return 0;
}

Device* find_device_name(const char *name)
{
    if (!first_dev)
        return 0;

    Device *dev = first_dev;
    do
    {
        if (strcmp(dev->name, name) == 0)
            return dev;

        dev = dev->next;
    } while (dev);

    return 0;
}

int main()
{
    setlinebuf(stdout);

    struct udev *ctx = udev_new();

    struct udev_monitor *mon = udev_monitor_new_from_netlink(ctx, "udev");

    int ret = udev_monitor_enable_receiving(mon);
    if (ret < 0)
    {
        printf("Failed to enable receiving: %d\n", ret);
        return 1;
    }

    int fd = udev_monitor_get_fd(mon);
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    while (1)
    {
        struct udev_device *dev = udev_monitor_receive_device(mon);
        if (dev == 0)
        {
            printf("Failed to get device\n");
            continue;
        }

        const char *path = udev_device_get_devpath(dev);
        const char *action = udev_device_get_action(dev);

        Device* device = find_device_path(path);

        if (strcmp(action, "add") == 0 && !device)
        {
            const char *name = udev_device_get_property_value(dev, "ID_MODEL_FROM_DATABASE");
            if (name && !find_device_name(name))
            {
                printf("%s device: %s at %s\n", action, name, path);

                add_device(path, name);
            }
        }

        if (strcmp(action, "remove") == 0 && device)
        {
            printf("remove device: %s\n", device->name);

            remove_device(device);
        }

        udev_device_unref(dev);
    }

    udev_monitor_unref(mon);
    udev_unref(ctx);
}