/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

/*
 * Code imported from usbutils: https://github.com/gregkh/usbutils
 */

#include <libudev.h>
#include <string.h>
#include <stdio.h>

static struct udev * udev = NULL;
static struct udev_hwdb * hwdb = NULL;

int names_init(void) __attribute__((constructor (101)));
int names_init(void) {
  udev = udev_new();
  if (udev == NULL) {
    return -1;
  }
  hwdb = udev_hwdb_new(udev);
  if (hwdb == NULL) {
    return -1;
  }

  return 0;
}

void names_exit(void) __attribute__((destructor (101)));
void names_exit(void) {
  hwdb = udev_hwdb_unref(hwdb);
  udev = udev_unref(udev);
}

static const char * hwdb_get(const char * modalias, const char * key) {
  struct udev_list_entry * entry;
  udev_list_entry_foreach(entry, udev_hwdb_get_properties_list_entry(hwdb, modalias, 0)) {
    if (strcmp(udev_list_entry_get_name(entry), key) == 0) {
      return udev_list_entry_get_value(entry);
    }
  }
  return NULL;
}

const char * names_vendor(u_int16_t vendorid) {
  char modalias[64];
  sprintf(modalias, "usb:v%04X*", vendorid);
  return hwdb_get(modalias, "ID_VENDOR_FROM_DATABASE");
}

const char * names_product(u_int16_t vendorid, u_int16_t productid) {
  char modalias[64];
  sprintf(modalias, "usb:v%04Xp%04X*", vendorid, productid);
  return hwdb_get(modalias, "ID_MODEL_FROM_DATABASE");
}

int get_vendor_string(char *buf, size_t size, u_int16_t vid)
{
        const char *cp;

        if (size < 1)
                return 0;
        *buf = 0;
        if (!(cp = names_vendor(vid)))
                return 0;
        return snprintf(buf, size, "%s", cp);
}

int get_product_string(char *buf, size_t size, u_int16_t vid, u_int16_t pid)
{
        const char *cp;

        if (size < 1)
                return 0;
        *buf = 0;
        if (!(cp = names_product(vid, pid)))
                return 0;
        return snprintf(buf, size, "%s", cp);
}
