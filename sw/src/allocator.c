/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <allocator.h>
#include <stdio.h>

#define BIND_ENDPOINT(MAP, DIR, SOURCE, TARGET) \
    ALLOCATOR_S2T_ENDPOINT(MAP, DIR | SOURCE) = (DIR | TARGET); \
    ALLOCATOR_T2S_ENDPOINT(MAP, DIR | TARGET) = (DIR | SOURCE);

#define PRINT_PROPERTY(DIR,CAP) \
    if (props->ep[i] & GUSB_EP_DIR_##DIR(GUSB_EP_CAP_##CAP)) { \
        printf(" X  "); \
    } else { \
        printf("    "); \
    }

void allocator_print_props(const s_ep_props * props) {

  unsigned char i;
  printf("EP IN INT BLK ISO OUT INT BLK ISO BIDIR\n");
  for (i = 0; i < sizeof(props->ep) / sizeof(*props->ep); ++i) {
    if (props->ep[i] != 0) {
      printf("%2hhu ", i + 1);
      printf("   ");
      PRINT_PROPERTY(IN, INT)
      PRINT_PROPERTY(IN, BLK)
      PRINT_PROPERTY(IN, ISO)
      printf("    ");
      PRINT_PROPERTY(OUT, INT)
      PRINT_PROPERTY(OUT, BLK)
      PRINT_PROPERTY(OUT, ISO)
      PRINT_PROPERTY(BIDIR, NONE)
      printf("\n");
    }
  }
}

void allocator_print_map(const s_endpoint_map * map) {

  unsigned char i;
  for (i = 0; i < sizeof(map->sourceToTarget[0]) / sizeof(*map->sourceToTarget[0]); ++i) {
    if (map->sourceToTarget[0][i] != 0) {
      printf("0x%02x -> 0x%02x\n", USB_DIR_OUT | (i + 1), map->sourceToTarget[0][i]);
    }
    if (map->sourceToTarget[1][i] != 0) {
      printf("0x%02x -> 0x%02x\n", USB_DIR_IN | (i + 1), map->sourceToTarget[1][i]);
    }
    if (map->sourceToTargetStub[0][i] != 0) {
      printf("0x%02x -> 0x%02x (stub)\n", USB_DIR_OUT | (i + 1), map->sourceToTargetStub[0][i]);
    }
    if (map->sourceToTargetStub[1][i] != 0) {
      printf("0x%02x -> 0x%02x (stub)\n", USB_DIR_IN | (i + 1), map->sourceToTargetStub[1][i]);
    }
  }
}

static int compare_endpoint_properties(const s_ep_props * src, const s_ep_props * dst) {

  unsigned char i;
  for (i = 0; i < sizeof(src->ep) / sizeof(*src->ep); ++i) {
    if (src->ep[i] != 0) {
      if ((dst->ep[i] & src->ep[i]) != src->ep[i]) {
        return 1;
      }
    }
  }

  return 0;
}

static unsigned char allocate_endpoint(const unsigned char src, s_ep_props * dst) {

  unsigned char i;
  for (i = 0; i < sizeof(dst->ep) / sizeof(*dst->ep); ++i) {
    if ((dst->ep[i] & src) == src) {
      if (src & GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL)) {
        if (dst->ep[i] & GUSB_EP_IN_USED) {
          continue;
        }
        if ((dst->ep[i] & GUSB_EP_OUT_USED) && (dst->ep[i] & GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE)) == 0) {
          continue;
        }
      }
      if (src & GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL)) {
        if (dst->ep[i] & GUSB_EP_OUT_USED) {
          continue;
        }
        if ((dst->ep[i] & GUSB_EP_IN_USED) && (dst->ep[i] & GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE)) == 0) {
          continue;
        }
      }
      if (src & GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL)) {
        dst->ep[i] |= GUSB_EP_IN_USED;
      }
      if (src & GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL)) {
        dst->ep[i] |= GUSB_EP_OUT_USED;
      }
      return i + 1;
    }
  }

  return 0;
}

static unsigned char allocate_stub(unsigned char sourceEndpoint, s_endpoint_map * map) {

  unsigned char targetIndex;
  for (targetIndex = 0; targetIndex < sizeof(*map->targetToSource) / sizeof(**map->targetToSource); ++targetIndex) {
    unsigned char target = (sourceEndpoint & USB_ENDPOINT_DIR_MASK) | (targetIndex + 1);
    if (ALLOCATOR_T2S_ENDPOINT(map, target) == 0x00) {
      unsigned char sourceIndex;
      for (sourceIndex = 0; sourceIndex < sizeof(*map->sourceToTargetStub) / sizeof(**map->sourceToTargetStub); ++sourceIndex) {
        unsigned char source = (sourceEndpoint & USB_ENDPOINT_DIR_MASK) | (sourceIndex + 1);
        if (ALLOCATOR_S2T_STUB_ENDPOINT(map, source) == target) {
          break;
        }
      }
      if (sourceIndex == sizeof(*map->sourceToTargetStub) / sizeof(**map->sourceToTargetStub)) {
        return target;
      }
    }
  }

  return 0;
}

int allocator_bind(const s_ep_props * source, s_ep_props * target, s_endpoint_map * map) {

  int renumber = compare_endpoint_properties(source, target);

  unsigned int endpointIndex;
  for (endpointIndex = 0; endpointIndex < sizeof(source->ep) / sizeof(*source->ep); ++endpointIndex) {
    unsigned char sourceNumber = endpointIndex + 1;
    unsigned char targetNumber = sourceNumber;
    if (source->ep[endpointIndex] & GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE)) {
      if (renumber) {
        // try to allocate a bidirectional endpoint first
        targetNumber = allocate_endpoint(source->ep[endpointIndex], target);
      }
      if (targetNumber != 0) {
        BIND_ENDPOINT(map, USB_DIR_IN, sourceNumber, targetNumber)
        BIND_ENDPOINT(map, USB_DIR_OUT, sourceNumber, targetNumber)
        continue;
      }
      // try to allocate two endpoints
    }
    if (source->ep[endpointIndex] & GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL)) {
      if (renumber) {
        targetNumber = allocate_endpoint(source->ep[endpointIndex] & GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL), target);
      }
      if (targetNumber != 0) {
        BIND_ENDPOINT(map, USB_DIR_IN, sourceNumber, targetNumber)
      }
    }
    if (source->ep[endpointIndex] & GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL)) {
      if (renumber) {
        targetNumber = allocate_endpoint(source->ep[endpointIndex] & GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL), target);
      }
      if (targetNumber != 0) {
        BIND_ENDPOINT(map, USB_DIR_OUT, sourceNumber, targetNumber)
      }
    }
  }

  // try to allocate stubs

  for (endpointIndex = 0; endpointIndex < sizeof(source->ep) / sizeof(*source->ep); ++endpointIndex) {
    unsigned char sourceNumber = endpointIndex + 1;
    if (source->ep[endpointIndex] & GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL)) {
      unsigned char source = USB_DIR_IN | sourceNumber;
      if (ALLOCATOR_S2T_ENDPOINT(map, source) == 0x00) {
        unsigned char target = allocate_stub(source, map);
        if (target != 0x00) {
          ALLOCATOR_S2T_STUB_ENDPOINT(map, source) = target;
        }
      }
    }
    if (source->ep[endpointIndex] & GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL)) {
      unsigned char source = USB_DIR_OUT | sourceNumber;
      if (ALLOCATOR_S2T_ENDPOINT(map, source) == 0x00) {
        unsigned char target = allocate_stub(source, map);
        if (target != 0x00) {
          ALLOCATOR_S2T_STUB_ENDPOINT(map, source) = target;
        }
      }
    }
  }

  return renumber;
}
