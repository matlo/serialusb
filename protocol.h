/*
 Copyright (c) 2015 Mathieu Laurendeau
 License: GPLv3
 */

#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#define BYTE_NO_PACKET    0x00
#define BYTE_DESCRIPTORS  0x11
#define BYTE_INDEX        0x22
#define BYTE_ENDPOINTS    0x33
#define BYTE_START        0x44
#define BYTE_CONTROL      0x55
#define BYTE_IN           0x66
#define BYTE_OUT          0x77

#define BYTE_RESET        0xee
#define BYTE_DEBUG        0xff

#define BYTE_LEN_0_BYTE   0x00
#define BYTE_LEN_1_BYTE   0x01

#endif
