/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <allocator.h>
#include <stdio.h>
#include <string.h>

static const s_ep_props x360Source = {
        {
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT),
        }
};

static const s_ep_props xOneSource = {
        {
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_ISO),
                GUSB_EP_DIR_OUT(GUSB_EP_CAP_ISO),
                GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_BLK),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_BLK),
        }
};

static const s_ep_props ds4Source = {
        {
                0,
                0,
                GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT),
        }
};

static const s_ep_props ds3Source = {
        {
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
        }
};

static const s_ep_props avr8Target = {
        {
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
        }
};

static const s_ep_props dummyHcdTarget = {
        {
                GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_BLK),
                GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_BLK),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_ISO) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ISO) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_BLK) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_BLK) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_ISO) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ISO) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_BLK) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_BLK) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_ISO) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ISO) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
                GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL) | GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE),
        }
};

static struct {
    const char * name;
    const s_ep_props * source;
    const s_ep_props * target;
    s_endpoint_map endpointMap;
    int renumber;
} testCases[] = {
        {
                .name = "x360 controller to avr8",
                .source = &x360Source,
                .target = &avr8Target,
                .endpointMap = {
                        .sourceToTarget =     { { 0x00, 0x02, 0x00, 0x04, 0x06, 0x00 }, { 0x81, 0x00, 0x83, 0x00, 0x85, 0x00 } },
                        .targetToSource =     { { 0x00, 0x02, 0x00, 0x04, 0x00, 0x05 }, { 0x81, 0x00, 0x83, 0x00, 0x85, 0x00 } },
                        .sourceToTargetStub = { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x82 } },
                },
                .renumber = 1
        },
        {
                .name = "xOne controller to avr8",
                .source = &xOneSource,
                .target = &avr8Target,
                .endpointMap = {
                        .sourceToTarget =     { { 0x00, 0x02, 0x00, 0x00, 0x00, 0x00 }, { 0x81, 0x00, 0x00, 0x00, 0x00, 0x00 } },
                        .targetToSource =     { { 0x00, 0x02, 0x00, 0x00, 0x00, 0x00 }, { 0x81, 0x00, 0x00, 0x00, 0x00, 0x00 } },
                        .sourceToTargetStub = { { 0x00, 0x00, 0x00, 0x01, 0x03, 0x00 }, { 0x00, 0x00, 0x82, 0x00, 0x83, 0x84 } },
                },
                .renumber = 1
        },
        {
                .name = "ds4 controller to avr8",
                .source = &ds4Source,
                .target = &avr8Target,
                .endpointMap = {
                        .sourceToTarget = { { 0x00, 0x00, 0x03, 0x00 }, { 0x00, 0x00, 0x00, 0x84 } },
                        .targetToSource = { { 0x00, 0x00, 0x03, 0x00 }, { 0x00, 0x00, 0x00, 0x84 } },
                },
                .renumber = 0
        },
        {
                .name = "ds3 controller to avr8",
                .source = &ds3Source,
                .target = &avr8Target,
                .endpointMap = {
                        .sourceToTarget = { { 0x00, 0x02 }, { 0x81 } },
                        .targetToSource = { { 0x00, 0x02 }, { 0x81 } },
                },
                .renumber = 0
        },
        {
                .name = "x360 controller to dummy_hcd",
                .source = &x360Source,
                .target = &dummyHcdTarget,
                .endpointMap = {
                        .sourceToTarget = { { 0x00, 0x03, 0x00, 0x05, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x84, 0x00, 0x85, 0x00, 0x8a, 0x87, 0x00, 0x00, 0x00, 0x00 } },
                        .targetToSource = { { 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x05 }, { 0x00, 0x00, 0x00, 0x81, 0x83, 0x00, 0x86, 0x00, 0x00, 0x85 } },
                },
                .renumber = 1
        },
        {
                .name = "xOne controller to dummy_hcd",
                .source = &xOneSource,
                .target = &dummyHcdTarget,
                .endpointMap = {
                        .sourceToTarget = { { 0x00, 0x03, 0x00, 0x04, 0x01 }, { 0x84, 0x00, 0x83, 0x00, 0x81, 0x82 } },
                        .targetToSource = { { 0x05, 0x00, 0x02, 0x04, 0x00 }, { 0x85, 0x86, 0x83, 0x81, 0x00, 0x00 } },
                },
                .renumber = 1
        },
        {
                .name = "ds4 controller to dummy_hcd",
                .source = &ds4Source,
                .target = &dummyHcdTarget,
                .endpointMap = {
                        .sourceToTarget = { { 0x00, 0x00, 0x03, 0x00 }, { 0x00, 0x00, 0x00, 0x84 } },
                        .targetToSource = { { 0x00, 0x00, 0x03, 0x00 }, { 0x00, 0x00, 0x00, 0x84 } },
                },
                .renumber = 0
        },
        {
                .name = "ds3 controller to dummy_hcd",
                .source = &ds3Source,
                .target = &dummyHcdTarget,
                .endpointMap = {
                        .sourceToTarget = { { 0x00, 0x03, 0x00 }, { 0x84, 0x00, 0x00, 0x00 } },
                        .targetToSource = { { 0x00, 0x00, 0x02 }, { 0x00, 0x00, 0x00, 0x81 } },
                },
                .renumber = 1
        }
};

int main(int argc __attribute__((unused)), char * argv[] __attribute__((unused))) {

  int ret = 0;

  unsigned int caseNumber;
  for (caseNumber = 0; caseNumber < sizeof(testCases) / sizeof(*testCases); ++caseNumber) {
      s_ep_props target = *testCases[caseNumber].target;
      s_endpoint_map endpointMap = { {}, {}, {} };
      int renumber = allocator_bind(testCases[caseNumber].source, &target, &endpointMap);
      if (renumber != testCases[caseNumber].renumber) {
          fprintf(stderr, "test \"%s\" failed: bad return value (expected %d, got %d)\n", testCases[caseNumber].name, testCases[caseNumber].renumber, renumber);
          continue;
      }
      if (memcmp(&endpointMap, &testCases[caseNumber].endpointMap, sizeof(endpointMap))) {
          fprintf(stderr, "test \"%s\" failed: endpointMap does not match expected result\n", testCases[caseNumber].name);
          fprintf(stderr, "result:\n");
          allocator_print_map(&endpointMap);
          fprintf(stderr, "expected:\n");
          allocator_print_map(&testCases[caseNumber].endpointMap);
          fprintf(stderr, "source:\n");
          allocator_print_props(testCases[caseNumber].source);
          fprintf(stderr, "target:\n");
          allocator_print_props(&target);
          continue;
      }
      printf("test \"%s\" succeeded\n", testCases[caseNumber].name);
  }

  return ret;
}
