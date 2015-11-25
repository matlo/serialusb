/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef USBASYNC_H_
#define USBASYNC_H_

#include <linux/usb/ch9.h>

#ifdef WIN32
#define PACKED __attribute__((gcc_struct, packed))
#else
#define PACKED __attribute__((packed))
#endif

struct usb_hid_descriptor
{
  unsigned char bLength;
  unsigned char bDescriptorType;
  unsigned short bcdHID;
  unsigned char bCountryCode;
  unsigned char bNumDescriptors;
  unsigned char bReportDescriptorType;
  unsigned short wReportDescriptorLength;
} PACKED;

typedef int (* USBASYNC_READ_CALLBACK)(int user, unsigned char endpoint, const void * buf, unsigned int count);
typedef int (* USBASYNC_WRITE_CALLBACK)(int user, unsigned char endpoint, int transfered);
typedef int (* USBASYNC_CLOSE_CALLBACK)(int user);
#ifndef WIN32
typedef void (* USBASYNC_REGISTER_SOURCE)(int fd, int id, int (*fp_read)(int), int (*fp_write)(int), int (*fp_cleanup)(int));
#else
typedef void (* USBASYNC_REGISTER_SOURCE)(HANDLE handle, int id, int (*fp_read)(int), int (*fp_write)(int), int (*fp_cleanup)(int));
#endif

struct p_altInterface {
  struct usb_interface_descriptor * descriptor;
  struct usb_hid_descriptor * hidDescriptor;
  unsigned char bNumEndpoints;
  struct usb_endpoint_descriptor ** endpoints; //bNumEndpoints elements
};

struct p_interface {
  unsigned char bNumAltInterfaces;
  struct p_altInterface * altInterfaces; //bNumAltInterfaces elements
};

struct p_configuration {
  unsigned char * raw;
  struct usb_config_descriptor * descriptor;
  struct p_interface * interfaces; //descriptor->bNumInterfaces elements
};

struct p_other {
  unsigned char type;
  unsigned char index;
  unsigned short length;
  unsigned char * data;
};

typedef struct {
    struct usb_device_descriptor device;
    struct p_configuration * configurations; //device.bNumConfigurations elements
    struct usb_string_descriptor langId0;
    unsigned int nbOthers;
    struct p_other * others; //nbOthers elements
} s_usb_descriptors;

typedef struct {
    unsigned short vendor_id;
    unsigned short product_id;
    char * path;
    int next;
} s_usb_dev;

int usbasync_open_ids(unsigned short vendor, unsigned short product);
s_usb_dev * usbasync_enumerate(unsigned short vendor, unsigned short product);
void usbasync_free_enumeration(s_usb_dev * usb_devs);
int usbasync_open_path(const char * path);
const s_usb_descriptors * usbasync_get_usb_descriptors(int device);
int usbasync_close(int device);
int usbasync_read_timeout(int device, unsigned char endpoint, void * buf, unsigned int count, unsigned int timeout);
int usbasync_register(int device, int user, USBASYNC_READ_CALLBACK fp_read, USBASYNC_WRITE_CALLBACK fp_write, USBASYNC_CLOSE_CALLBACK fp_close, USBASYNC_REGISTER_SOURCE fp_register);
int usbasync_write(int device, unsigned char endpoint, const void * buf, unsigned int count);
int usbasync_write_timeout(int device, unsigned char endpoint, const void * buf, unsigned int count, unsigned int timeout);
int usbasync_print_descriptors(int device);

#endif /* USBASYNC_H_ */
