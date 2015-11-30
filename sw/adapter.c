/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <adapter.h>
#include <serialasync.h>
#include <string.h>
#include <stdio.h>

#define MAX_ADAPTERS 7

#define PRINT_ERROR_OTHER(msg) fprintf(stderr, "%s:%d %s: %s\n", __FILE__, __LINE__, __func__, msg);

static struct {
  s_packet packet;
  unsigned int bread;
  int serial;
  ADAPTER_PACKET_CALLBACK fp_packet_cb;
} adapters[MAX_ADAPTERS];

void adapter_init(void) __attribute__((constructor (101)));
void adapter_init(void) {
  unsigned int i;
  for (i = 0; i < sizeof(adapters) / sizeof(*adapters); ++i) {
    adapters[i].serial = -1;
  }
}

static inline int adapter_check(int adapter, const char * file, unsigned int line, const char * func) {
  if (adapter < 0 || adapter >= MAX_ADAPTERS) {
    fprintf(stderr, "%s:%d %s: invalid device\n", file, line, func);
    return -1;
  }
  if (adapters[adapter].serial < 0) {
    fprintf(stderr, "%s:%d %s: no such adapter\n", file, line, func);
    return -1;
  }
  return 0;
}
#define ADAPTER_CHECK(device,retValue) \
  if(adapter_check(device, __FILE__, __LINE__, __func__) < 0) { \
    return retValue; \
  }

int adapter_recv(int adapter, const void * buf, unsigned int count) {

  ADAPTER_CHECK(adapter, -1)

  int ret = 0;

  if(adapters[adapter].bread + count < sizeof(s_packet)) {
    memcpy((unsigned char *)&adapters[adapter].packet + adapters[adapter].bread, buf, count);
    adapters[adapter].bread += count;
    unsigned int remaining;
    if(adapters[adapter].bread < sizeof(s_header))
    {
      remaining = sizeof(s_header) - adapters[adapter].bread;
    }
    else
    {
      remaining = adapters[adapter].packet.header.length - (adapters[adapter].bread - sizeof(s_header));
    }
    if(remaining == 0)
    {
      ret = adapters[adapter].fp_packet_cb(adapter, &adapters[adapter].packet);
      adapters[adapter].bread = 0;
      serialasync_set_read_size(adapters[adapter].serial, sizeof(s_header));
    }
    else
    {
      serialasync_set_read_size(adapters[adapter].serial, remaining);
    }
  }
  else
  {
    // this is a critical error (no possible recovering)
    fprintf(stderr, "%s:%d %s: invalid data size (count=%u, available=%zu)\n", __FILE__, __LINE__, __func__, count, sizeof(s_packet) - adapters[adapter].bread);
    return -1;
  }

  return ret;
}

int adapter_send(int adapter, unsigned char type, const unsigned char * data, unsigned int count, unsigned int timeout) {

  ADAPTER_CHECK(adapter, -1)

  if (count != 0 && data == NULL) {
    PRINT_ERROR_OTHER("data is NULL")
    return -1;
  }

  do {

    unsigned char length = sizeof(((s_packet *)NULL)->value);
    if(count < length) {

      length = count;
    }
    s_packet packet = { .header = { .type = type, .length = length } };
    if (data) {
      memcpy(packet.value, data, length);
    }
    data += length;
    count -= length;

    int ret;
    if(timeout > 0) {
      ret = serialasync_write_timeout(adapters[adapter].serial, &packet, 2 + length, timeout);
    } else {
      ret = serialasync_write(adapters[adapter].serial, &packet, 2 + length);
    }
    if(ret < 0) {
      return -1;
    }
  } while (count > 0);

  return 0;
}

int adapter_add(int serial, ADAPTER_PACKET_CALLBACK fp_packet_cb) {

  unsigned int i;
  for (i = 0; i < sizeof(adapters) / sizeof(*adapters); ++i) {
    if (adapters[i].serial < 0) {
      adapters[i].serial = serial;
      adapters[i].fp_packet_cb = fp_packet_cb;
      return i;
    }
  }

  return -1;
}
