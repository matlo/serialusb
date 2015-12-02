/*
 Copyright (c) 2015 Mathieu Laurendeau
 License: GPLv3
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdint.h>
#include <limits.h>

#ifdef WIN32
#define PACKED __attribute__((gcc_struct, packed))
#else
#define PACKED __attribute__((packed))
#endif

#define MAX_DESCRIPTORS_SIZE 1024

#define MAX_DESCRIPTORS 32 // should not exceed 255

#define MAX_ENDPOINTS 6 // excluding the control endpoint

#define MAX_PACKET_SIZE_EP0 64

#define MAX_PAYLOAD_SIZE_EP 64 // for non-control endpoints

typedef struct PACKED {
  uint16_t offset;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
} s_descriptorIndex;

typedef struct PACKED {
  uint8_t number; // 0 means end of table
  uint8_t type;
  uint8_t size;
} s_endpointConfig;

typedef struct PACKED {
  uint8_t endpoint; // 0 means nothing to send
  uint8_t data[MAX_PAYLOAD_SIZE_EP];
} s_endpointPacket; // should not exceed 255 bytes

typedef enum {
  E_TYPE_DESCRIPTORS,
  E_TYPE_INDEX,
  E_TYPE_ENDPOINTS,
  E_TYPE_RESET,
  E_TYPE_CONTROL,
  E_TYPE_IN,
  E_TYPE_OUT,
  E_TYPE_DEBUG,
} e_packetType;

#define BYTE_LEN_0_BYTE   0x00
#define BYTE_LEN_1_BYTE   0x01

#define BUFFER_SIZE (UCHAR_MAX+1)

typedef struct PACKED {
  unsigned char type;
  unsigned char length;
} s_header;

typedef struct PACKED
{
  s_header header;
  unsigned char value[BUFFER_SIZE - sizeof(s_header)];
} s_packet;

#endif
