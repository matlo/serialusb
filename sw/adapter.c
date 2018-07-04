/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <adapter.h>
#include <gimxserial/include/gserial.h>
#include <gimxpoll/include/gpoll.h>
#include <string.h>
#include <stdio.h>
#include <gimxcommon/include/glist.h>
#include <gimxcommon/include/gerror.h>
#include <gimxlog/include/glog.h>
#include <stdlib.h>

GLOG_GET(GLOG_NAME)

#define USART_BAUDRATE 500000

struct adapter_device {
  s_packet packet;
  unsigned int bread;
  struct gserial_device * serial;
  ADAPTER_READ_CALLBACK read_callback;
  ADAPTER_WRITE_CALLBACK write_callback;
  ADAPTER_CLOSE_CALLBACK close_callback;
  GLIST_LINK(struct adapter_device)
};

GLIST_INST(struct adapter_device, adapter_devices, adapter_close)

static int adapter_recv(void * user, const void * buf, int status) {

  struct adapter_device * device = (struct adapter_device *) user;

  if (status < 0) {
    return -1;
  }

  int ret = 0;

  if(device->bread + status <= sizeof(s_packet)) {
    memcpy((unsigned char *)&device->packet + device->bread, buf, status);
    device->bread += status;
    unsigned int remaining;
    if(device->bread < sizeof(s_header))
    {
      remaining = sizeof(s_header) - device->bread;
    }
    else
    {
      remaining = device->packet.header.length - (device->bread - sizeof(s_header));
    }
    if(remaining == 0)
    {
      ret = device->read_callback(device, &device->packet);
      device->bread = 0;
      gserial_set_read_size(device->serial, sizeof(s_header));
    }
    else
    {
      gserial_set_read_size(device->serial, remaining);
    }
  }
  else
  {
    // this is a critical error (no possible recovering)
    fprintf(stderr, "%s:%d %s: invalid data size (count=%u, available=%zu)\n", __FILE__, __LINE__, __func__, status, sizeof(s_packet) - device->bread);
    return -1;
  }

  return ret;
}

int adapter_send(void * user, unsigned char type, const unsigned char * data, unsigned int count) {

  struct adapter_device * device = (struct adapter_device *) user;

  if (count != 0 && data == NULL) {
    PRINT_ERROR_OTHER("data is NULL")
    return -1;
  }

  do {

    unsigned char length = MAX_PACKET_VALUE_SIZE;
    if(count < length) {

      length = count;
    }
    s_packet packet = { .header = { .type = type, .length = length } };
    if (data) {
      memcpy(packet.value, data, length);
    }
    data += length;
    count -= length;

    int ret = gserial_write(device->serial, &packet, 2 + length);
    if(ret < 0) {
      return -1;
    }
  } while (count > 0);

  return 0;
}

struct adapter_device * adapter_open(const char * port, const ADAPTER_CALLBACKS * callbacks) {

  if (callbacks->fp_register == NULL) {
    PRINT_ERROR_OTHER("fp_register is NULL")
    return NULL;
  }

  if (callbacks->fp_remove == NULL) {
    PRINT_ERROR_OTHER("fp_remove is NULL")
    return NULL;
  }

  gserial_init();

  struct gserial_device * serial = gserial_open(port, USART_BAUDRATE);
  if (serial == NULL) {
    gserial_exit();
    return NULL;
  }

  struct adapter_device * device = calloc(1, sizeof(*device));
  if (device == NULL) {
    PRINT_ERROR_ALLOC_FAILED("calloc")
    gserial_close(serial);
    gserial_exit();
    return NULL;
  }

  GSERIAL_CALLBACKS serial_callbacks = {
    .fp_read = adapter_recv,
    .fp_write = callbacks->fp_write,
    .fp_close = adapter_close,
    .fp_register = callbacks->fp_register,
    .fp_remove = callbacks->fp_remove
  };
  int ret = gserial_register(serial, device, &serial_callbacks);
  if (ret < 0) {
    gserial_close(serial);
    gserial_exit();
    free(device);
    return NULL;
  }

  device->serial = serial;
  device->read_callback = callbacks->fp_read;
  device->write_callback = callbacks->fp_write;
  device->close_callback = callbacks->fp_close;

  GLIST_ADD(adapter_devices, device)

  return device;
}

int adapter_close(void * user) {

  struct adapter_device * device = (struct adapter_device *) user;

  gserial_close(device->serial);

  gserial_exit();

  GLIST_REMOVE(adapter_devices, device)

  free(device);

  return 0;
}
