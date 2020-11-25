/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <gimxusb/include/gusb.h>
#include <gimxadapter/include/gadapter.h>
#include <allocator.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gimxpoll/include/gpoll.h>
#include <gimxtimer/include/gtimer.h>
#include <names.h>
#include <gimxprio/include/gprio.h>
#include <sys/time.h>
#include <unistd.h>
#include <stddef.h>
#include <gimxlog/include/glog.h>

GLOG_INST(proxy)

#ifdef WIN32
#define REGISTER_FUNCTION gpoll_register_handle
#define REMOVE_FUNCTION gpoll_remove_handle
#else
#define REGISTER_FUNCTION gpoll_register_fd
#define REMOVE_FUNCTION gpoll_remove_fd
#endif

#define ENDPOINT_MAX_NUMBER USB_ENDPOINT_NUMBER_MASK

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"

#define PRINT_ERROR_OTHER(MESSAGE) fprintf(stderr, "%s:%d %s: %s\n", __FILE__, __LINE__, __func__, MESSAGE);
#define PRINT_TRANSFER_WRITE_ERROR(ENDPOINT,MESSAGE) fprintf(stderr, "%s:%d %s: write transfer failed on endpoint %hhu with error: %s\n", __FILE__, __LINE__, __func__, ENDPOINT & USB_ENDPOINT_NUMBER_MASK, MESSAGE);
#define PRINT_TRANSFER_READ_ERROR(ENDPOINT,MESSAGE) fprintf(stderr, "%s:%d %s: read transfer failed on endpoint %hhu with error: %s\n", __FILE__, __LINE__, __func__, ENDPOINT & USB_ENDPOINT_NUMBER_MASK, MESSAGE);

static struct gusb_device * usb = NULL;
static struct gadapter_device * adapter= NULL;
static struct gtimer * init_timer = NULL;

static s_usb_descriptors descriptors;
static unsigned char desc[GA_MAX_DESCRIPTORS_SIZE] = {};
static unsigned char * pDesc = desc;
static s_ga_descriptorIndex descIndex[GA_MAX_DESCRIPTORS] = {};
static s_ga_descriptorIndex * pDescIndex = descIndex;
static s_ga_endpointConfig endpoints[GA_MAX_ENDPOINTS] = {};
static s_ga_endpointConfig * pEndpoints = endpoints;

static uint8_t descIndexSent = 0;
static uint8_t endpointsSent = 0;

static uint8_t inPending = 0;

static s_endpoint_map endpointMap = { {}, {}, {} };

static struct {
  uint16_t length;
  s_ga_endpointPacket packet;
} inPackets[ENDPOINT_MAX_NUMBER] = {};

static uint8_t inEpFifo[GA_MAX_ENDPOINTS] = {};
static uint8_t nbInEpFifo = 0;

static volatile int done;

/*
 * the atmega32u4 supports up to 6 non-control endpoints
 * that can be IN or OUT (not BIDIR),
 * and only the INTERRUPT type is supported.
 */
static s_ep_props avr8Target = {
  {
    GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
    GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
    GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
    GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
    GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
    GUSB_EP_DIR_IN(GUSB_EP_CAP_INT) | GUSB_EP_DIR_OUT(GUSB_EP_CAP_INT),
  }
};

static int send_next_in_packet() {

  if (inPending) {
    return 0;
  }

  if (nbInEpFifo > 0) {
    uint8_t inPacketIndex = ALLOCATOR_ENDPOINT_ADDR_TO_INDEX(inEpFifo[0]);
    int ret = gadapter_send(adapter, GA_TYPE_IN, (const void *)&inPackets[inPacketIndex].packet, inPackets[inPacketIndex].length);
    if(ret < 0) {
      return -1;
    }
    inPending = inEpFifo[0];
    --nbInEpFifo;
    memmove(inEpFifo, inEpFifo + 1, nbInEpFifo * sizeof(*inEpFifo));
  }

  return 0;
}

static int queue_in_packet(unsigned char endpoint, const void * buf, int transfered) {

  if (nbInEpFifo == sizeof(inEpFifo) / sizeof(*inEpFifo)) {
    PRINT_ERROR_OTHER("no more space in inEpFifo")
    return -1;
  }

  uint8_t inPacketIndex = ALLOCATOR_ENDPOINT_ADDR_TO_INDEX(endpoint);
  inPackets[inPacketIndex].packet.endpoint = ALLOCATOR_S2T_ENDPOINT(&endpointMap, endpoint);
  memcpy(inPackets[inPacketIndex].packet.data, buf, transfered);
  inPackets[inPacketIndex].length = transfered + 1;
  inEpFifo[nbInEpFifo] = endpoint;
  ++nbInEpFifo;

  /*
   * TODO MLA: Poll the endpoint after registering the packet?
   */

  return 0;
}

int usb_read_callback(void * user __attribute__((unused)), unsigned char endpoint, const void * buf, int status) {

  switch (status) {
  case E_STATUS_TRANSFER_TIMED_OUT:
    PRINT_TRANSFER_READ_ERROR(endpoint, "TIMEOUT")
    break;
  case E_STATUS_TRANSFER_STALL:
    break;
  case E_STATUS_TRANSFER_ERROR:
    PRINT_TRANSFER_WRITE_ERROR(endpoint, "OTHER ERROR")
    return -1;
  case E_STATUS_NO_DEVICE:
    PRINT_TRANSFER_WRITE_ERROR(endpoint, "NO DEVICE")
    return -1;
  default:
    break;
  }

  if (endpoint == 0) {

    int ret = 0;
    if (status >= 0) {
      if (adapter != NULL) {
        if (status > (int)MAX_PACKET_VALUE_SIZE) {
          PRINT_ERROR_OTHER("too many bytes transfered")
          done = 1;
          return -1;
        }
        ret = gadapter_send(adapter, GA_TYPE_CONTROL, buf, status);
      }
    } else {
      if (adapter != NULL) {
        ret = gadapter_send(adapter, GA_TYPE_CONTROL_STALL, NULL, 0);
      }
    }
    if(ret < 0) {
      return -1;
    }
  } else {
    if (status >= 0) {
      if (adapter != NULL) {
        if (status > GA_MAX_PAYLOAD_SIZE_EP) {
          PRINT_ERROR_OTHER("too many bytes transfered")
          done = 1;
          return -1;
        }
        int ret = queue_in_packet(endpoint, buf, status);
        if (ret < 0) {
          done = 1;
          return -1;
        }

        ret = send_next_in_packet();
        if (ret < 0) {
          done = 1;
          return -1;
        }
      }
    }
  }

  return 0;
}

static int source_poll_all_endpoints() {

  int ret = 0;
  unsigned char i;
  for (i = 0; i < sizeof(*endpointMap.targetToSource) / sizeof(**endpointMap.targetToSource) && ret >= 0; ++i) {
    uint8_t endpoint = ALLOCATOR_T2S_ENDPOINT(&endpointMap, USB_DIR_IN | (i + 1));
    if (endpoint) {
      ret = gusb_poll(usb, endpoint);
    }
  }
  return ret;
}

int source_write_callback(void * user __attribute__((unused)), unsigned char endpoint, int status) {

  switch (status) {
  case E_STATUS_TRANSFER_TIMED_OUT:
    PRINT_TRANSFER_WRITE_ERROR(endpoint, "TIMEOUT")
    break;
  case E_STATUS_TRANSFER_STALL:
    if (endpoint == 0) {
      if (adapter != NULL) {
        int ret = gadapter_send(adapter, GA_TYPE_CONTROL_STALL, NULL, 0);
        if (ret < 0) {
          done = 1;
          return -1;
        }
      }
    }
    break;
  case E_STATUS_TRANSFER_ERROR:
    PRINT_TRANSFER_WRITE_ERROR(endpoint, "OTHER ERROR")
    return -1;
  case E_STATUS_NO_DEVICE:
    PRINT_TRANSFER_WRITE_ERROR(endpoint, "NO DEVICE")
    return -1;
  default:
    if (endpoint == 0) {
      if (adapter != NULL) {
        int ret = gadapter_send(adapter, GA_TYPE_CONTROL, NULL, 0);
        if (ret < 0) {
          done = 1;
          return -1;
        }
      }
    }
    break;
  }

  return 0;
}

int source_close_callback(void * user __attribute__((unused))) {

  done = 1;
  return 1;
}

int adapter_send_callback(void * user __attribute__((unused)), int transfered) {

  if (transfered < 0) {
    done = 1;
    return 1;
  }

  return 0;
}

int adapter_close_callback(void * user __attribute__((unused))) {

  done = 1;
  return 1;
}

static char * source_select() {

  char * path = NULL;

  struct gusb_device_info * usb_devs = gusb_enumerate(0x0000, 0x0000);
  if (usb_devs == NULL) {
    fprintf(stderr, "No USB device detected!\n");
    return NULL;
  }
  printf("Available USB devices:\n");
  unsigned int index = 0;
  char vendor[128], product[128];
  struct gusb_device_info * current;
  for (current = usb_devs; current != NULL; current = current->next) {
    get_vendor_string(vendor, sizeof(vendor), current->vendor_id);
    get_product_string(product, sizeof(product), current->vendor_id, current->product_id);
    printf("%2d", index++);
    printf(" VID 0x%04x (%s)", current->vendor_id, strlen(vendor) ? vendor : "unknown vendor");
    printf(" PID 0x%04x (%s)", current->product_id, strlen(product) ? product : "unknown product");
    printf(" PATH %s\n", current->path);
  }

  printf("Select the USB device number: ");
  unsigned int choice = UINT_MAX;
  if (scanf("%d", &choice) == 1 && choice < index) {
    for (current = usb_devs; current != NULL && choice != 0; current = current->next) {
        --choice;
    }
    path = strdup(current->path);
    if(path == NULL) {
      fprintf(stderr, "can't duplicate path.\n");
    }
  } else {
    fprintf(stderr, "Invalid choice.\n");
  }

  gusb_free_enumeration(usb_devs);

  return path;
}

static void get_endpoint_properties(unsigned char configurationIndex, s_ep_props * props) {

  struct p_configuration * pConfiguration = descriptors.configurations + configurationIndex;
  unsigned char interfaceIndex;
  for (interfaceIndex = 0; interfaceIndex < pConfiguration->descriptor->bNumInterfaces; ++interfaceIndex) {
    struct p_interface * pInterface = pConfiguration->interfaces + interfaceIndex;
    unsigned char altInterfaceIndex;
    for (altInterfaceIndex = 0; altInterfaceIndex < pInterface->bNumAltInterfaces; ++altInterfaceIndex) {
      struct p_altInterface * pAltInterface = pInterface->altInterfaces + altInterfaceIndex;
      unsigned char endpointIndex;
      for (endpointIndex = 0; endpointIndex < pAltInterface->bNumEndpoints; ++endpointIndex) {
        struct usb_endpoint_descriptor * endpoint =
                pConfiguration->interfaces[interfaceIndex].altInterfaces[altInterfaceIndex].endpoints[endpointIndex];
        uint8_t epIndex = ALLOCATOR_ENDPOINT_ADDR_TO_INDEX(endpoint->bEndpointAddress);
        uint8_t prop = 0;
        switch (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
        case USB_ENDPOINT_XFER_INT:
          prop = GUSB_EP_CAP_INT;
          break;
        case USB_ENDPOINT_XFER_BULK:
          prop = GUSB_EP_CAP_BLK;
          break;
        case USB_ENDPOINT_XFER_ISOC:
          prop = GUSB_EP_CAP_ISO;
          break;
        }
        if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) {
          props->ep[epIndex] |= GUSB_EP_DIR_IN(prop);
        } else {
          props->ep[epIndex] |= GUSB_EP_DIR_OUT(prop);
        }
        if ((props->ep[epIndex] & GUSB_EP_DIR_IN(GUSB_EP_CAP_ALL)) && (props->ep[epIndex] & GUSB_EP_DIR_OUT(GUSB_EP_CAP_ALL))) {
          props->ep[epIndex] |= GUSB_EP_DIR_BIDIR(GUSB_EP_CAP_NONE);
        }
      }
    }
  }
}

static int fix_configuration(unsigned char configurationIndex) {

  pEndpoints = endpoints;

  if (configurationIndex >= descriptors.device.bNumConfigurations) {
    PRINT_ERROR_OTHER("invalid configuration index")
    return -1;
  }

  struct p_configuration * pConfiguration = descriptors.configurations + configurationIndex;
  printf("configuration: %hhu\n", pConfiguration->descriptor->bConfigurationValue);
  unsigned char interfaceIndex;
  for (interfaceIndex = 0; interfaceIndex < pConfiguration->descriptor->bNumInterfaces; ++interfaceIndex) {
    struct p_interface * pInterface = pConfiguration->interfaces + interfaceIndex;
    unsigned char altInterfaceIndex;
    for (altInterfaceIndex = 0; altInterfaceIndex < pInterface->bNumAltInterfaces; ++altInterfaceIndex) {
      struct p_altInterface * pAltInterface = pInterface->altInterfaces + altInterfaceIndex;
      printf("  interface: %hhu:%hhu\n", pAltInterface->descriptor->bInterfaceNumber, pAltInterface->descriptor->bAlternateSetting);
      unsigned char bNumEndpoints = pAltInterface->bNumEndpoints;
      unsigned char endpointIndex;
      for (endpointIndex = 0; endpointIndex < pAltInterface->bNumEndpoints; ++endpointIndex) {
        struct usb_endpoint_descriptor * endpoint =
            descriptors.configurations[configurationIndex].interfaces[interfaceIndex].altInterfaces[altInterfaceIndex].endpoints[endpointIndex];
        uint8_t sourceEndpoint = endpoint->bEndpointAddress;
        unsigned char targetEndpoint = ALLOCATOR_S2T_ENDPOINT(&endpointMap, sourceEndpoint);
        endpoint->bEndpointAddress = targetEndpoint;
        printf("    endpoint:");
        printf(" %s", ((sourceEndpoint & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) ? "IN" : "OUT");
        printf(" %s",
            (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT ? "INTERRUPT" :
            (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK ? "BULK" :
            (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_ISOC ? "ISOCHRONOUS" : "UNKNOWN");
        printf(" %hu", sourceEndpoint & USB_ENDPOINT_NUMBER_MASK);
        if (sourceEndpoint != targetEndpoint) {
          if (targetEndpoint == 0x00) {
            targetEndpoint = ALLOCATOR_S2T_STUB_ENDPOINT(&endpointMap, sourceEndpoint);
            if (targetEndpoint == 0x00) {
              printf(KRED" -> no stub available"KNRM"\n");
              endpoint->bDescriptorType = 0x00;
              --bNumEndpoints;
            } else {
              printf(KRED" -> %hu (stub)"KNRM"\n", targetEndpoint & USB_ENDPOINT_NUMBER_MASK);
              endpoint->bEndpointAddress = targetEndpoint;
            }
            continue;
          } else {
              printf(KRED" -> %hu"KNRM, targetEndpoint & USB_ENDPOINT_NUMBER_MASK);
          }
        } else {
          printf(" -> %hu", targetEndpoint & USB_ENDPOINT_NUMBER_MASK);
        }
        printf("\n");
        if (pEndpoints - endpoints < (ptrdiff_t)(sizeof(endpoints) / sizeof(*endpoints))) {
          pEndpoints->number = endpoint->bEndpointAddress;
          pEndpoints->type = endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
          pEndpoints->size = endpoint->wMaxPacketSize;
          ++pEndpoints;
        }
      }
      if (bNumEndpoints != pAltInterface->bNumEndpoints) {
          printf(KRED"    bNumEndpoints: %hhu -> %hhu"KNRM"\n", pAltInterface->bNumEndpoints, bNumEndpoints);
          pAltInterface->bNumEndpoints = bNumEndpoints;
      }
    }
  }

  return 0;
}

static int add_descriptor(uint16_t wValue, uint16_t wIndex, uint16_t wLength, void * data) {

  if (pDesc + wLength > desc + GA_MAX_DESCRIPTORS_SIZE || pDescIndex >= descIndex + GA_MAX_DESCRIPTORS) {
    fprintf(stderr, "%s:%d %s: unable to add descriptor wValue=0x%04x wIndex=0x%04x wLength=%u (available=%u)\n",
        __FILE__, __LINE__, __func__, wValue, wIndex, wLength, (unsigned int)(GA_MAX_DESCRIPTORS_SIZE - (pDesc - desc)));
    return -1;
  }

  pDescIndex->offset = pDesc - desc;
  pDescIndex->wValue = wValue;
  pDescIndex->wIndex = wIndex;
  pDescIndex->wLength = wLength;
  memcpy(pDesc, data, wLength);
  pDesc += wLength;
  ++pDescIndex;

  return 0;
}

int send_descriptors() {

  int ret;

  ret = add_descriptor((USB_DT_DEVICE << 8), 0, sizeof(descriptors.device), &descriptors.device);
  if (ret < 0) {
    return -1;
  }

  ret = add_descriptor((USB_DT_STRING << 8), 0, sizeof(descriptors.langId0), &descriptors.langId0);
  if (ret < 0) {
    return -1;
  }

  unsigned int descNumber;
  for(descNumber = 0; descNumber < descriptors.device.bNumConfigurations; ++descNumber) {

    ret = add_descriptor((USB_DT_CONFIG << 8) | descNumber, 0, descriptors.configurations[descNumber].descriptor->wTotalLength, descriptors.configurations[descNumber].raw);
    if (ret < 0) {
      return -1;
    }
  }

  for(descNumber = 0; descNumber < descriptors.nbOthers; ++descNumber) {

    ret = add_descriptor(descriptors.others[descNumber].wValue, descriptors.others[descNumber].wIndex, descriptors.others[descNumber].wLength, descriptors.others[descNumber].data);
    if (ret < 0) {
      return -1;
    }
  }

  ret = gadapter_send(adapter, GA_TYPE_DESCRIPTORS, desc, pDesc - desc);
  if (ret < 0) {
    return -1;
  }

  return 0;
}

static int send_index() {

  if (descIndexSent) {
    return 0;
  }

  descIndexSent = 1;

  return gadapter_send(adapter, GA_TYPE_INDEX, (unsigned char *)&descIndex, (pDescIndex - descIndex) * sizeof(*descIndex));
}

static int send_endpoints() {

  if (endpointsSent) {
    return 0;
  }

  endpointsSent = 1;

  return gadapter_send(adapter, GA_TYPE_ENDPOINTS, (unsigned char *)&endpoints, (pEndpoints - endpoints) * sizeof(*endpoints));
}

static int source_send_out_transfer(unsigned char endpoint, const void * buf, unsigned int length) {

  endpoint = ALLOCATOR_T2S_ENDPOINT(&endpointMap, endpoint);
  if (endpoint == 0) {
    PRINT_ERROR_OTHER("OUT transfer directed to a stubbed endpoint")
    return -1;
  }

  return gusb_write(usb, endpoint, buf, length);
}

static int source_send_control_transfer(struct usb_ctrlrequest * setup, unsigned int length) {

  if ((setup->bRequestType & USB_RECIP_MASK) == USB_RECIP_ENDPOINT) {
    if (setup->wIndex != 0) {
      setup->wIndex = ALLOCATOR_T2S_ENDPOINT(&endpointMap, setup->wIndex);
      if (setup->wIndex == 0) {
        PRINT_ERROR_OTHER("control transfer directed to a stubbed endpoint")
        return 0;
      }
    }
  }

  if (setup->bRequestType == USB_DIR_IN
      && setup->bRequest == USB_REQ_GET_DESCRIPTOR
      && (setup->wValue >> 8) == USB_DT_DEVICE_QUALIFIER) {
    // device qualifier descriptor is for high speed devices
    printf("force stall for get device qualifier\n");
    return gadapter_send(adapter, GA_TYPE_CONTROL_STALL, NULL, 0);
  }

  return gusb_write(usb, 0, setup, length);
}

static void dump(unsigned char * data, unsigned char length)
{
  int i;
  for (i = 0; i < length; ++i) {
    if(i && !(i % 8)) {
      printf("\n");
    }
    printf("0x%02x ", data[i]);
  }
  printf("\n");
}

static int process_packet(void * user __attribute__((unused)), s_ga_packet * packet)
{
  unsigned char type = packet->header.type;

  int ret = 0;

  switch (packet->header.type) {
  case GA_TYPE_DESCRIPTORS:
    ret = send_index();
    break;
  case GA_TYPE_INDEX:
    ret = send_endpoints();
    break;
  case GA_TYPE_ENDPOINTS:
    gtimer_close(init_timer);
    init_timer = NULL;
    printf("Proxy started successfully. Press ctrl+c to stop it.\n");
    ret = source_poll_all_endpoints();
    break;
  case GA_TYPE_IN:
    if (inPending > 0) {
      ret = gusb_poll(usb, inPending);
      inPending = 0;
      if (ret != -1) {
        ret = send_next_in_packet();
      }
    }
    break;
  case GA_TYPE_OUT:
    {
        s_ga_endpointPacket * epPacket = (s_ga_endpointPacket *)packet->value;
      ret = source_send_out_transfer(epPacket->endpoint, epPacket->data, packet->header.length - 1);
    }
    break;
  case GA_TYPE_CONTROL:
    {
      struct usb_ctrlrequest * setup = (struct usb_ctrlrequest *)packet->value;
      ret = source_send_control_transfer(setup, packet->header.length);
    }
    break;
  case GA_TYPE_DEBUG:
    {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      printf("%ld.%06ld debug packet received (size = %d bytes)\n", tv.tv_sec, tv.tv_usec, packet->header.length);
      dump(packet->value, packet->header.length);
    }
    break;
  case GA_TYPE_RESET:
    ret = -1;
    break;
  default:
    {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      fprintf(stderr, "%ld.%06ld ", tv.tv_sec, tv.tv_usec);
      fprintf(stderr, "unhandled packet (type=0x%02x)\n", type);
    }
    break;
  }

  if(ret < 0) {
    done = 1;
  }

  return ret;
}

int proxy_init() {

  char * path = source_select();

  if(path == NULL) {
    fprintf(stderr, "No USB device selected!\n");
    return -1;
  }

  usb = gusb_open_path(path);

  if (usb == NULL) {
    free(path);
    return -1;
  }

  const s_usb_descriptors * desc = gusb_get_usb_descriptors(usb);
  if (desc == NULL) {
    free(path);
    return -1;
  }

  printf("Opened device: VID 0x%04x PID 0x%04x PATH %s\n", desc->device.idVendor, desc->device.idProduct, path);

  free(path);

  if (desc->device.bNumConfigurations == 0) {
    PRINT_ERROR_OTHER("missing configuration")
    return -1;
  }

  if (desc->configurations[0].descriptor->bNumInterfaces == 0) {
    PRINT_ERROR_OTHER("missing interface")
    return -1;
  }

  if (desc->configurations[0].interfaces[0].bNumAltInterfaces == 0) {
    PRINT_ERROR_OTHER("missing altInterface")
    return -1;
  }

  descriptors = *desc;

  return 0;
}

static int timer_close(void * user __attribute__((unused))) {
  done = 1;
  return 1;
}

static int timer_read(void * user __attribute__((unused))) {
  /*
   * Returning a non-zero value will make gpoll return,
   * this allows to check the 'done' variable.
   */
  return 1;
}

int proxy_start(const char * port) {

  if (port != NULL) {

      printf("Target capabilities:\n");

      allocator_print_props(&avr8Target);

      s_ep_props source = { { } };
      get_endpoint_properties(0, &source);

      printf("Source requirements:\n");
      allocator_print_props(&source);

      allocator_bind(&source, &avr8Target, &endpointMap);

      if (fix_configuration(0) < 0) {
        return -1;
      }

      GADAPTER_CALLBACKS callbacks = {
              .fp_read = process_packet,
              .fp_write = adapter_send_callback,
              .fp_close = adapter_close_callback,
              .fp_register = REGISTER_FUNCTION,
              .fp_remove = REMOVE_FUNCTION
      };

      adapter = gadapter_open(port, GA_USART_BAUDRATE, NULL, &callbacks);

      if(adapter == NULL) {
        return -1;
      }

      if (send_descriptors() < 0) {
        return -1;
      }

      GTIMER_CALLBACKS timer_callbacks = {
          .fp_read = timer_close,
          .fp_close = timer_close,
          .fp_register = gpoll_register_fd,
          .fp_remove = gpoll_remove_fd
      };
      init_timer = gtimer_start(NULL, 1000000, &timer_callbacks);
      if (init_timer == NULL) {
        return -1;
      }
  }

  GUSB_CALLBACKS usb_callbacks = {
      .fp_read = usb_read_callback,
      .fp_write = source_write_callback,
      .fp_close = source_close_callback,
      .fp_register = gpoll_register_fd,
      .fp_remove = gpoll_remove_fd
  };
  int ret = gusb_register(usb, 0, &usb_callbacks);
  if (ret < 0) {
    return -1;
  }

  GTIMER_CALLBACKS timer_callbacks = {
      .fp_read = timer_read,
      .fp_close = timer_close,
      .fp_register = gpoll_register_fd,
      .fp_remove = gpoll_remove_fd
  };
  struct gtimer * timer = gtimer_start(NULL, 10000, &timer_callbacks);
  if (timer == NULL) {
    return -1;
  }

  if (gprio_init() < 0) {
    PRINT_ERROR_OTHER("Failed to set process priority!")
    return -1;
  }

  while (!done) {
    gpoll();
  }

  gprio_clean();

  gtimer_close(timer);

  if (adapter != NULL) {
      gadapter_send(adapter, GA_TYPE_RESET, NULL, 0);
      usleep(10000); // leave time for the reset packet to be sent
      gadapter_close(adapter);
  }

  gusb_close(usb);

  if (init_timer != NULL) {
    PRINT_ERROR_OTHER("Failed to start the proxy: initialization timeout expired!")
    gtimer_close(init_timer);
    return -1;
  }

  return 0;
}

void proxy_stop() {
  done = 1;
}
