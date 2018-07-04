/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef ALLOCATOR_H_
#define ALLOCATOR_H_

#include <gimxusb/include/gusb.h>

#define ALLOCATOR_MAX_ENDPOINT_NUMBER USB_ENDPOINT_NUMBER_MASK

#define ALLOCATOR_ENDPOINT_ADDR_TO_INDEX(ENDPOINT) (((ENDPOINT) & USB_ENDPOINT_NUMBER_MASK) - 1)
#define ALLOCATOR_ENDPOINT_DIR_TO_INDEX(ENDPOINT) ((ENDPOINT) >> 7)

#define ALLOCATOR_T2S_ENDPOINT(MAP, ENDPOINT) ((MAP)->targetToSource[ALLOCATOR_ENDPOINT_DIR_TO_INDEX(ENDPOINT)][ALLOCATOR_ENDPOINT_ADDR_TO_INDEX(ENDPOINT)])
#define ALLOCATOR_S2T_ENDPOINT(MAP, ENDPOINT) ((MAP)->sourceToTarget[ALLOCATOR_ENDPOINT_DIR_TO_INDEX(ENDPOINT)][ALLOCATOR_ENDPOINT_ADDR_TO_INDEX(ENDPOINT)])

#define ALLOCATOR_S2T_STUB_ENDPOINT(MAP, ENDPOINT) ((MAP)->sourceToTargetStub[ALLOCATOR_ENDPOINT_DIR_TO_INDEX(ENDPOINT)][ALLOCATOR_ENDPOINT_ADDR_TO_INDEX(ENDPOINT)])

typedef struct {
  unsigned char sourceToTarget[2][ALLOCATOR_MAX_ENDPOINT_NUMBER];
  unsigned char targetToSource[2][ALLOCATOR_MAX_ENDPOINT_NUMBER];
  unsigned char sourceToTargetStub[2][ALLOCATOR_MAX_ENDPOINT_NUMBER];
} s_endpoint_map;


int allocator_bind(const s_ep_props * src, s_ep_props * dst, s_endpoint_map * map);
void allocator_print_props(const s_ep_props * props);
void allocator_print_map(const s_endpoint_map * map);

#endif /* ALLOCATOR_H_ */
