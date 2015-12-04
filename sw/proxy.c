/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <usbasync.h>
#include <serialasync.h>
#include <protocol.h>
#include <adapter.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GE.h>

#define PRINT_ERROR_OTHER(msg) fprintf(stderr, "%s:%d %s: %s\n", __FILE__, __LINE__, __func__, msg);

static int usb = -1;
static int adapter = -1;

static s_usb_descriptors * descriptors = NULL;
static unsigned char desc[MAX_DESCRIPTORS_SIZE] = {};
static unsigned char * pDesc = desc;
static s_descriptorIndex descIndex[MAX_DESCRIPTORS] = {};
static s_descriptorIndex * pDescIndex = descIndex;
static s_endpointConfig endpoints[MAX_ENDPOINTS] = {};
static s_endpointConfig * pEndpoints = endpoints;

static uint8_t descIndexSent = 0;
static uint8_t endpointsSent = 0;

static uint8_t inEndpoints[MAX_ENDPOINTS] = {};
static uint8_t inEndpointNumber = 0;
static uint8_t endpointMap[LIBUSB_ENDPOINT_ADDRESS_MASK] = {};
static uint8_t inPending = 0;

/*
 * TODO MLA: Have a slot per endpoint instead of a queue.
 */
static struct {
  unsigned short length;
  s_endpointPacket packet;
  uint8_t sourceEndpoint;
} inEnpointPackets[MAX_ENDPOINTS] = {};
static uint8_t nbEpInData = 0;

#define TRACE printf("%s\n", __func__);

extern volatile int done;

static void set_done() {
  done = 1;
}

static int send_next_in_packet() {

  if (inPending) {
    return 0;
  }

  if (nbEpInData > 0) {
    int ret = adapter_send(adapter, E_TYPE_IN, (const void *)&inEnpointPackets->packet, inEnpointPackets->length);
    if(ret < 0) {
      return -1;
    }
    inPending = inEnpointPackets->sourceEndpoint;
    memmove(inEnpointPackets, inEnpointPackets + 1, (--nbEpInData) * sizeof(*inEnpointPackets));
  }

  return 0;
}

static int queue_in_packet(unsigned char endpoint, const void * buf, int transfered) {

  if (nbEpInData == sizeof(inEnpointPackets) / sizeof(*inEnpointPackets)) {
    PRINT_ERROR_OTHER("no slot available")
    return -1;
  }

  inEnpointPackets[nbEpInData].packet.endpoint = endpoints[endpointMap[(endpoint & LIBUSB_ENDPOINT_ADDRESS_MASK) - 1]].number;
  memcpy(inEnpointPackets[nbEpInData].packet.data, buf, transfered);
  inEnpointPackets[nbEpInData].length = transfered + 1;
  inEnpointPackets[nbEpInData].sourceEndpoint = endpoint;
  ++nbEpInData;

  //TODO MLA: Poll the endpoint after registering the packet.

  return 0;
}

int usb_read_callback(int user, unsigned char endpoint, const void * buf, int transfered) {

  /*
   * TODO MLA: For control transfers, send the status of the request.
   */
  if (transfered < 0) {
    set_done();
    return -1;
  }

  uint8_t endpointAddress = endpoint & LIBUSB_ENDPOINT_ADDRESS_MASK;

  if (endpointAddress == 0) {

    if (transfered > MAX_PACKET_SIZE) {
      PRINT_ERROR_OTHER("too many bytes transfered")
      set_done();
      return -1;
    }

    int ret = adapter_send(adapter, E_TYPE_CONTROL, buf, transfered);
    if(ret < 0) {
      return -1;
    }
  } else {

    if (transfered > MAX_PAYLOAD_SIZE_EP) {
      PRINT_ERROR_OTHER("too many bytes transfered")
      set_done();
      return -1;
    }

    int ret = queue_in_packet(endpoint, buf, transfered);
    if (ret < 0) {
      set_done();
      return -1;
    }

    ret = send_next_in_packet();
    if (ret < 0) {
      set_done();
      return -1;
    }
  }

  return 0;
}

int usb_write_callback(int user, unsigned char endpoint, int transfered) {

  TRACE
  //TODO MLA

  return 0;
}

int usb_close_callback(int user) {

  set_done();
  return 1;
}

int adapter_send_callback(int user, int transfered) {

  if (transfered < 0) {
    set_done();
    return 1;
  }

  return 0;
}

int adapter_close_callback(int user) {

  set_done();
  return 1;
}

#define ADD_DESCRIPTOR(WVALUE,WINDEX,WLENGTH,DATA) \
  if (pDesc + WLENGTH <= desc + MAX_DESCRIPTORS_SIZE && pDescIndex < descIndex + MAX_DESCRIPTORS) { \
    pDescIndex->offset = pDesc - desc; \
    pDescIndex->wValue = WVALUE; \
    pDescIndex->wIndex = WINDEX; \
    pDescIndex->wLength = WLENGTH; \
    memcpy(pDesc, DATA, WLENGTH); \
    pDesc += WLENGTH; \
    ++pDescIndex; \
  } else { \
    warn = 1; \
  }

static char * usb_select() {

  char * path = NULL;

  s_usb_dev * usb_devs = usbasync_enumerate(0x0000, 0x0000);
  if (usb_devs == NULL) {
    fprintf(stderr, "No USB device detected!\n");
    return NULL;
  }
  printf("Available USB devices:\n");
  unsigned int index = 0;
  s_usb_dev * current;
  for (current = usb_devs; current != NULL; ++current) {
    printf("%2d VID 0x%04x PID 0x%04x PATH %s\n", index++, current->vendor_id, current->product_id, current->path);
    if (current->next == 0) {
      break;
    }
  }

  printf("Select the USB device number: ");
  unsigned int choice = UINT_MAX;
  if (scanf("%d", &choice) == 1 && choice < index) {
    path = strdup(usb_devs[choice].path);
    if(path == NULL) {
      fprintf(stderr, "can't duplicate path.\n");
    }
  } else {
    fprintf(stderr, "Invalid choice.\n");
  }

  usbasync_free_enumeration(usb_devs);

  return path;
}

void fix_endpoints() {

  /*
   * TODO MLA:
   * Endpoints should not be renumbered whenever possible.
   * A good strategy would be to renumber OUT endpoints only.
   */

  pEndpoints = endpoints;

  unsigned char configurationIndex;
  for (configurationIndex = 0; configurationIndex < descriptors->device.bNumConfigurations; ++configurationIndex) {
    unsigned char endpointNumber = 0;
    struct p_configuration * pConfiguration = descriptors->configurations + configurationIndex;
    printf("configuration: %hhu\n", pConfiguration->descriptor->bConfigurationValue);
    unsigned char interfaceIndex;
    for (interfaceIndex = 0; interfaceIndex < pConfiguration->descriptor->bNumInterfaces; ++interfaceIndex) {
      struct p_interface * pInterface = pConfiguration->interfaces + interfaceIndex;
      unsigned char altInterfaceIndex;
      for (altInterfaceIndex = 0; altInterfaceIndex < pInterface->bNumAltInterfaces; ++altInterfaceIndex) {
        struct p_altInterface * pAltInterface = pInterface->altInterfaces + altInterfaceIndex;
        printf("  interface: %hhu:%hhu\n", pAltInterface->descriptor->bInterfaceNumber, pAltInterface->descriptor->bAlternateSetting);
        unsigned char endpointIndex;
        for (endpointIndex = 0; endpointIndex < pAltInterface->bNumEndpoints; ++endpointIndex) {
          struct usb_endpoint_descriptor * endpoint =
              descriptors->configurations[configurationIndex].interfaces[interfaceIndex].altInterfaces[altInterfaceIndex].endpoints[endpointIndex];
          uint8_t originalEndpoint = endpoint->bEndpointAddress;
          ++endpointNumber;
          endpoint->bEndpointAddress = (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) | endpointNumber;
          printf("    endpoint:");
          printf(" %s", ((endpoint->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) ? "IN" : "OUT");
          printf(" %s",
              (endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_INTERRUPT ? "INTERRUPT" :
              (endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK ? "BULK" :
              (endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS ?
                  "ISOCHRONOUS" : "UNKNOWN");
          printf(" %hu", originalEndpoint & LIBUSB_ENDPOINT_ADDRESS_MASK);
          if (originalEndpoint != endpoint->bEndpointAddress) {
            printf(" -> %hu", endpointNumber);
          } else {
            printf(" -> unchanged");
          }
          printf("\n");
          if ((originalEndpoint & LIBUSB_ENDPOINT_ADDRESS_MASK) == 0) {
            PRINT_ERROR_OTHER("invalid endpoint number")
            continue;
          }
          if (configurationIndex > 0) {
            continue;
          }
          if ((endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_INTERRUPT) {
            printf("      endpoint %hu won't be configured (not an INTERRUPT endpoint)\n", endpoint->bEndpointAddress & LIBUSB_ENDPOINT_ADDRESS_MASK);
            continue;
          }
          if (endpoint->wMaxPacketSize > MAX_PAYLOAD_SIZE_EP) {
            printf("      endpoint %hu won't be configured (max packet size %hu > %hu)\n", endpoint->bEndpointAddress & LIBUSB_ENDPOINT_ADDRESS_MASK, endpoint->wMaxPacketSize, MAX_PAYLOAD_SIZE_EP);
            continue;
          }
          if (endpointNumber > MAX_ENDPOINTS) {
            printf("      endpoint %hu won't be configured (endpoint number %hhu > %hhu)\n", endpoint->bEndpointAddress & LIBUSB_ENDPOINT_ADDRESS_MASK, endpointNumber, MAX_ENDPOINTS);
            continue;
          }
          if ((endpoint->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
            inEndpoints[inEndpointNumber++] = originalEndpoint;
            endpointMap[(originalEndpoint & LIBUSB_ENDPOINT_ADDRESS_MASK) - 1] = endpointNumber - 1;
          }
          pEndpoints->number = endpoint->bEndpointAddress;
          pEndpoints->type = endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
          pEndpoints->size = endpoint->wMaxPacketSize;
          ++pEndpoints;
        }
      }
    }
  }
}

void fix_device() {

  if (descriptors->device.bcdUSB != BCD_USB_1_1) {
    printf("bcdUSB: 0x%04x -> 0x%04x\n", descriptors->device.bcdUSB, BCD_USB_1_1);
    descriptors->device.bcdUSB = BCD_USB_1_1;
  }
  if (descriptors->device.bMaxPacketSize0 != MAX_PACKET_SIZE_EP0) {
    printf("bMaxPacketSize0: %hhu -> %hhu\n", descriptors->device.bMaxPacketSize0, MAX_PACKET_SIZE_EP0);
    descriptors->device.bMaxPacketSize0 = MAX_PACKET_SIZE_EP0;
  }
}

int send_descriptors() {

  unsigned char warn = 0;

  ADD_DESCRIPTOR((LIBUSB_DT_DEVICE << 8), 0, sizeof(descriptors->device), &descriptors->device)
  ADD_DESCRIPTOR((LIBUSB_DT_STRING << 8), 0, sizeof(descriptors->langId0), &descriptors->langId0)

  unsigned int descNumber;
  for(descNumber = 0; descNumber < descriptors->device.bNumConfigurations; ++descNumber) {

    ADD_DESCRIPTOR((LIBUSB_DT_CONFIG << 8) | descNumber, 0, descriptors->configurations[descNumber].descriptor->wTotalLength, descriptors->configurations[descNumber].raw)
  }

  for(descNumber = 0; descNumber < descriptors->nbOthers; ++descNumber) {

    ADD_DESCRIPTOR(descriptors->others[descNumber].wValue, descriptors->others[descNumber].wIndex, descriptors->others[descNumber].wLength, descriptors->others[descNumber].data)
  }

  if (warn) {
    PRINT_ERROR_OTHER("unable to add all descriptors")
  }

  int ret = adapter_send(adapter, E_TYPE_DESCRIPTORS, desc, pDesc - desc);
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

  return adapter_send(adapter, E_TYPE_INDEX, (unsigned char *)&descIndex, (pDescIndex - descIndex) * sizeof(*descIndex));
}

static int send_endpoints() {

  if (endpointsSent) {
    return 0;
  }

  endpointsSent = 1;

  return adapter_send(adapter, E_TYPE_ENDPOINTS, (unsigned char *)&endpoints, (pEndpoints - endpoints) * sizeof(*endpoints));
}

static int poll_all_endpoints() {

  int ret = 0;
  unsigned char i;
  for (i = 0; i < inEndpointNumber && ret >= 0; ++i) {
    ret = usbasync_poll(usb, inEndpoints[i]);
  }
  return ret;
}

static int send_out_packet(s_packet * packet) {

  s_endpointPacket * epPacket = (s_endpointPacket *)packet->value;

  return usbasync_write(usb, epPacket->endpoint, epPacket->data, packet->header.length - 1);
}

static int send_control_packet(s_packet * packet) {

  return usbasync_write(usb, 0, packet->value, packet->header.length);
}

static int process_packet(int user, s_packet * packet)
{
  unsigned char type = packet->header.type;

  int ret = 0;

  switch (packet->header.type) {
  case E_TYPE_DESCRIPTORS:
    ret = send_index();
    break;
  case E_TYPE_INDEX:
    ret = send_endpoints();
    break;
  case E_TYPE_ENDPOINTS:
    ret = poll_all_endpoints();
    break;
  case E_TYPE_IN:
    if (inPending > 0) {
      ret = usbasync_poll(usb, inPending);
      inPending = 0;
      if (ret != -1) {
        ret = send_next_in_packet();
      }
    }
    break;
  case E_TYPE_OUT:
    ret = send_out_packet(packet);
    break;
  case E_TYPE_CONTROL:
    ret = send_control_packet(packet);
    break;
/*  case E_TYPE_DEBUG:
    {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      printf("%ld.%06ld debug packet received (size = %d bytes)\n", tv.tv_sec, tv.tv_usec, length);
      dump(data, length);
    }
    break;
  case E_TYPE_RESET:
    //TODO MLA
    break;*/
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
    set_done();
  }

  return ret;
}

int proxy_init(char * port) {

  char * path = usb_select();

  if(path == NULL) {
    fprintf(stderr, "No USB device selected!\n");
    return -1;
  }

  usb = usbasync_open_path(path);

  if (usb < 0) {
    free(path);
    return -1;
  }

  descriptors = usbasync_get_usb_descriptors(usb);
  if (descriptors == NULL) {
    free(path);
    return -1;
  }

  printf("Opened device: VID 0x%04x PID 0x%04x PATH %s\n", descriptors->device.idVendor, descriptors->device.idProduct, path);

  free(path);

  if (descriptors->device.bNumConfigurations == 0) {
    PRINT_ERROR_OTHER("missing configuration")
    return -1;
  }

  if (descriptors->configurations[0].descriptor->bNumInterfaces == 0) {
    PRINT_ERROR_OTHER("missing interface")
    return -1;
  }

  if (descriptors->configurations[0].interfaces[0].bNumAltInterfaces == 0) {
    PRINT_ERROR_OTHER("missing altInterface")
    return -1;
  }

  if (descriptors->configurations[0].interfaces[0].altInterfaces[0].bNumEndpoints == 0) {
    PRINT_ERROR_OTHER("missing endpoint")
    return -1;
  }

  fix_device();
  fix_endpoints();

  if (port != NULL) {

    adapter = adapter_open(port, process_packet, adapter_send_callback, adapter_close_callback);

    if(adapter < 0) {
      return -1;
    }

    if (send_descriptors() < 0) {
      return -1;
    }

    int ret = usbasync_register(usb, 0, usb_read_callback, usb_write_callback, usb_close_callback, GE_AddSource);
    if (ret < 0) {
      return -1;
    }
  }

  return 0;
}

int proxy_stop() {

  if (adapter >= 0) {
    adapter_send(adapter, E_TYPE_RESET, NULL, 0);
  }
  if (usb >= 0) {
    usbasync_close(usb);
  }

  return 0;
}
