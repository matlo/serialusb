/*
 Copyright (c) 2015 Mathieu Laurendeau
 License: GPLv3
 */

#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#define MAX_DESCRIPTORS_SIZE 1024

#define MAX_DESCRIPTORS 32 // code is optimized when MAX_DESCRIPTORS <= 255 / 5

#if MAX_DESCRIPTORS > 255 / 5
#define uintDescIndex uint16_t
#else
#define uintDescIndex uint8_t
#endif

#define MAX_ENDPOINTS 6

#define MAX_EP_SIZE 64

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t number;
    uint8_t offset;
    uint16_t size;
} s_descIndex;

typedef struct __attribute__((packed)) {
    uint8_t number; // 0 means end of table
    uint8_t type;
    uint8_t size;
} s_endpoint;

typedef struct __attribute__((packed)) {
    uint8_t endpoint; // 0 means nothing to send
    uint8_t data[MAX_EP_SIZE];
} s_epData;

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

#endif
