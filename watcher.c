#include <libudev.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <malloc.h>
#include <libnotify/notify.h>
#include <canberra.h>

typedef struct Device
{
    char *path;
    char *name;
    struct Device *next, *prev;
} Device;

Device *first_dev, *last_dev;
ca_context *audioctx;

Device *add_device(const char *path, const char *name)
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

    return dev;
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

Device *find_device_path(const char *path)
{
    if (!first_dev || !path)
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

Device *find_device_name(const char *name)
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

int should_ignore(const char *vendor, const char *product)
{
    if (!vendor || !product)
        return 1;

    return strcmp(vendor, "1a40") == 0 && strcmp(product, "0101") == 0;
}

void sound_done(ca_context *c, uint32_t id, int error_code, void *userdata)
{
}

void play_sound(int added)
{
    const char *eventID = added ? "device-added" : "device-removed";

    ca_proplist *props;
    ca_proplist_create(&props);
    ca_proplist_sets(props, CA_PROP_EVENT_ID, eventID);
    ca_proplist_sets(props, CA_PROP_EVENT_DESCRIPTION, added ? "Device plugged in" : "Device unplugged");
    ca_proplist_sets(props, CA_PROP_CANBERRA_VOLUME, "10.0");

    ca_context_play_full(audioctx, 0, props, sound_done, props);
    ca_proplist_destroy(props);
}

void notify_connection(Device *dev, int added)
{
    const char *msg = added ? "Plugged in" : "Unplugged";
    NotifyNotification *notif = notify_notification_new(msg, dev->name, 0);
    notify_notification_show(notif, 0);
    g_object_unref(notif);

    play_sound(added);
}

int main()
{
    setlinebuf(stdout);

    if (!notify_init("USB Notify"))
    {
        printf("failed to initialize libnotify\n");
        return 1;
    }

    if (ca_context_create(&audioctx) != 0)
    {
        printf("failed to create audio context");
        return 1;
    }

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

        const char *path = udev_device_get_property_value(dev, "ID_PATH");
        if (!path)
        {
            udev_device_unref(dev);
            continue;
        }

        const char *action = udev_device_get_action(dev);

        Device *device = find_device_path(path);

        if (strcmp(action, "add") == 0 && !device)
        {
            const char *model = udev_device_get_property_value(dev, "ID_MODEL_FROM_DATABASE");
            if (model)
            {
                const char *vendor = udev_device_get_sysattr_value(dev, "idVendor");
                const char *product = udev_device_get_sysattr_value(dev, "idProduct");

                if (!should_ignore(vendor, product))
                {
                    printf("added device %s:%s: %s at %s\n", vendor, product, model, path);

                    Device *dev = add_device(path, model);
                    notify_connection(dev, 1);
                }
            }
        }

        if (strcmp(action, "remove") == 0 && device)
        {
            printf("removed device: %s\n", device->name);

            notify_connection(device, 0);

            remove_device(device);
        }

        udev_device_unref(dev);
    }

    notify_uninit();

    udev_monitor_unref(mon);
    udev_unref(ctx);
}
