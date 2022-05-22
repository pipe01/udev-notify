#include <libudev.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <malloc.h>
#include <libnotify/notify.h>
#include <canberra.h>

#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>

typedef struct Device
{
    char *path;
    char *model;
    char *vendor;
    struct Device *next, *prev;
} Device;

Device *first_dev, *last_dev;
ca_context *audioctx;

Device *add_device(const char *path, const char *model, const char *vendor)
{
    Device *dev = malloc(sizeof(Device));
    dev->model = strdup(model);
    dev->vendor = strdup(vendor);
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

    free(dev->model);
    free(dev->vendor);
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
        if (strcmp(dev->model, name) == 0)
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
    ca_proplist *props;
    ca_proplist_create(&props);
    ca_proplist_sets(props, CA_PROP_EVENT_ID, added ? "device-added" : "device-removed");
    ca_proplist_sets(props, CA_PROP_EVENT_DESCRIPTION, added ? "Device plugged in" : "Device unplugged");
    ca_proplist_sets(props, CA_PROP_CANBERRA_VOLUME, "10.0");

    ca_context_play_full(audioctx, 0, props, sound_done, props);
    ca_proplist_destroy(props);
}

void notify_connection(Device *dev, int added)
{
    const char *title = added ? "Plugged in" : "Unplugged";
    char *body = malloc(strlen(dev->model) + strlen(dev->vendor) + 2);
    sprintf(body, "%s\n%s", dev->vendor, dev->model);

    NotifyNotification *notif = notify_notification_new(title, body, 0);
    notify_notification_show(notif, 0);
    g_object_unref(notif);

    play_sound(added);
}

int create_signalfd(int *fd)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        return 1;

    *fd = signalfd(-1, &mask, 0);
    if (*fd == -1)
        return 1;

    return 0;
}

int main()
{
    int exit_code = 0;

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
        printf("failed to enable receiving: %d\n", ret);
        return 1;
    }

    int udev_fd = udev_monitor_get_fd(mon);
    int sig_fd = 0;

    if (create_signalfd(&sig_fd) != 0)
    {
        printf("failed to create signal fd\n");
        return 1;
    }

    struct pollfd *fds = calloc(2, sizeof(struct pollfd));
    fds[0].fd = udev_fd;
    fds[0].events = POLLIN;
    fds[1].fd = sig_fd;
    fds[1].events = POLLIN;

    printf("watching devices\n");

    while (1)
    {
        int n = poll(fds, 2, -1);

        if (n == -1)
        {
            printf("failed to poll for devices");
            exit_code = 1;
            break;
        }
        if (n == 0)
            continue;

        if (fds[1].revents & POLLIN)
        {
            struct signalfd_siginfo fdsi;
            ssize_t n = read(sig_fd, &fdsi, sizeof(fdsi));

            if (n == sizeof(fdsi) && fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGQUIT)
            {
                exit_code = 0;
                break;
            }

            continue;
        }

        if (!(fds[0].revents & POLLIN))
            continue;

        struct udev_device *dev = udev_monitor_receive_device(mon);
        if (dev == 0)
        {
            printf("failed to receive device\n");
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
            const char *vendor = udev_device_get_property_value(dev, "ID_VENDOR_FROM_DATABASE");

            if (model)
            {
                const char *vendorID = udev_device_get_sysattr_value(dev, "idVendor");
                const char *productID = udev_device_get_sysattr_value(dev, "idProduct");

                if (!should_ignore(vendorID, productID))
                {
                    printf("added device %s:%s: %s at %s\n", vendorID, productID, vendor, path);

                    Device *dev = add_device(path, model, vendor);
                    notify_connection(dev, 1);
                }
            }
        }
        else if (strcmp(action, "remove") == 0 && device)
        {
            printf("removed device: %s\n", device->model);

            notify_connection(device, 0);

            remove_device(device);
        }

        udev_device_unref(dev);
    }

    printf("exiting\n");

    free(fds);

    notify_uninit();

    udev_monitor_unref(mon);
    udev_unref(ctx);

    return exit_code;
}
