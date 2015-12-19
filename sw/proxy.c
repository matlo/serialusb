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
#include <gpoll.h>
#include <gtimer.h>
#include <names.h>
#include <prio.h>

#define ENDPOINT_MAX_NUMBER USB_ENDPOINT_NUMBER_MASK

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"

#define PRINT_ERROR_OTHER(MESSAGE) fprintf(stderr, "%s:%d %s: %s\n", __FILE__, __LINE__, __func__, MESSAGE);
#define PRINT_TRANSFER_WRITE_ERROR(ENDPOINT,MESSAGE) fprintf(stderr, "%s:%d %s: write transfer failed on endpoint %hhu with error: %s\n", __FILE__, __LINE__, __func__, ENDPOINT & USB_ENDPOINT_NUMBER_MASK, MESSAGE);
#define PRINT_TRANSFER_READ_ERROR(ENDPOINT,MESSAGE) fprintf(stderr, "%s:%d %s: read transfer failed on endpoint %hhu with error: %s\n", __FILE__, __LINE__, __func__, ENDPOINT & USB_ENDPOINT_NUMBER_MASK, MESSAGE);

static int usb = -1;
static int adapter = -1;
static int init_timer = -1;

static s_usb_descriptors * descriptors = NULL;
static unsigned char desc[MAX_DESCRIPTORS_SIZE] = {};
static unsigned char * pDesc = desc;
static s_descriptorIndex descIndex[MAX_DESCRIPTORS] = {};
static s_descriptorIndex * pDescIndex = descIndex;
static s_endpointConfig endpoints[MAX_ENDPOINTS] = {};
static s_endpointConfig * pEndpoints = endpoints;

static uint8_t descIndexSent = 0;
static uint8_t endpointsSent = 0;

static uint8_t inPending = 0;

static uint8_t serialToUsbEndpoint[2][ENDPOINT_MAX_NUMBER] = {};
static uint8_t usbToSerialEndpoint[2][ENDPOINT_MAX_NUMBER] = {};

#define ENDPOINT_ADDR_TO_INDEX(ENDPOINT) (((ENDPOINT) & USB_ENDPOINT_NUMBER_MASK) - 1)
#define ENDPOINT_DIR_TO_INDEX(ENDPOINT) ((ENDPOINT) >> 7)
#define S2U_ENDPOINT(ENDPOINT) serialToUsbEndpoint[ENDPOINT_DIR_TO_INDEX(ENDPOINT)][ENDPOINT_ADDR_TO_INDEX(ENDPOINT)]
#define U2S_ENDPOINT(ENDPOINT) usbToSerialEndpoint[ENDPOINT_DIR_TO_INDEX(ENDPOINT)][ENDPOINT_ADDR_TO_INDEX(ENDPOINT)]

static struct {
  uint16_t length;
  s_endpointPacket packet;
} inPackets[ENDPOINT_MAX_NUMBER] = {};

static uint8_t inEpFifo[MAX_ENDPOINTS] = {};
static uint8_t nbInEpFifo = 0;

static volatile int done;

#define EP_PROP_IN    (1 << 0)
#define EP_PROP_OUT   (1 << 1)
#define EP_PROP_BIDIR (1 << 2)

#define EP_PROP_INT   (1 << 3)
#define EP_PROP_BLK   (1 << 4)
#define EP_PROP_ISO   (1 << 5)

/*
 * the atmega32u4 supports up to 6 non-control endpoints
 * that can be IN or OUT (not BIDIR),
 * and only the INTERRUPT type is supported.
 */
static uint8_t targetProperties[ENDPOINT_MAX_NUMBER] = {
  EP_PROP_IN | EP_PROP_OUT | EP_PROP_INT,
  EP_PROP_IN | EP_PROP_OUT | EP_PROP_INT,
  EP_PROP_IN | EP_PROP_OUT | EP_PROP_INT,
  EP_PROP_IN | EP_PROP_OUT | EP_PROP_INT,
  EP_PROP_IN | EP_PROP_OUT | EP_PROP_INT,
  EP_PROP_IN | EP_PROP_OUT | EP_PROP_INT,
};

static int send_next_in_packet() {

  if (inPending) {
    return 0;
  }

  if (nbInEpFifo > 0) {
    uint8_t inPacketIndex = ENDPOINT_ADDR_TO_INDEX(inEpFifo[0]);
    int ret = adapter_send(adapter, E_TYPE_IN, (const void *)&inPackets[inPacketIndex].packet, inPackets[inPacketIndex].length);
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

  uint8_t inPacketIndex = ENDPOINT_ADDR_TO_INDEX(endpoint);
  inPackets[inPacketIndex].packet.endpoint = U2S_ENDPOINT(endpoint);
  memcpy(inPackets[inPacketIndex].packet.data, buf, transfered);
  inPackets[inPacketIndex].length = transfered + 1;
  inEpFifo[nbInEpFifo] = endpoint;
  ++nbInEpFifo;

  /*
   * TODO MLA: Poll the endpoint after registering the packet?
   */

  return 0;
}

int usb_read_callback(int user, unsigned char endpoint, const void * buf, int status) {

  switch (status) {
  case E_TRANSFER_TIMED_OUT:
    PRINT_TRANSFER_READ_ERROR(endpoint, "TIMEOUT")
    break;
  case E_TRANSFER_STALL:
    break;
  case E_TRANSFER_ERROR:
    PRINT_TRANSFER_WRITE_ERROR(endpoint, "OTHER ERROR")
    return -1;
  default:
    break;
  }

  if (endpoint == 0) {

    if (status > (int)MAX_PACKET_VALUE_SIZE) {
      PRINT_ERROR_OTHER("too many bytes transfered")
      done = 1;
      return -1;
    }

    int ret;
    if (status > 0) {
      ret = adapter_send(adapter, E_TYPE_CONTROL, buf, status);
    } else {
      ret = adapter_send(adapter, E_TYPE_CONTROL_STALL, NULL, 0);
    }
    if(ret < 0) {
      return -1;
    }
  } else {

    if (status > MAX_PAYLOAD_SIZE_EP) {
      PRINT_ERROR_OTHER("too many bytes transfered")
      done = 1;
      return -1;
    }

    if (status > 0) {

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

  return 0;
}

int usb_write_callback(int user, unsigned char endpoint, int status) {

  switch (status) {
  case E_TRANSFER_TIMED_OUT:
    PRINT_TRANSFER_WRITE_ERROR(endpoint, "TIMEOUT")
    break;
  case E_TRANSFER_STALL:
    if (endpoint == 0) {
      int ret = adapter_send(adapter, E_TYPE_CONTROL_STALL, NULL, 0);
      if (ret < 0) {
        done = 1;
        return -1;
      }
    }
    break;
  case E_TRANSFER_ERROR:
    PRINT_TRANSFER_WRITE_ERROR(endpoint, "OTHER ERROR")
    return -1;
  default:
    if (endpoint == 0) {
      int ret = adapter_send(adapter, E_TYPE_CONTROL, NULL, 0);
      if (ret < 0) {
        done = 1;
        return -1;
      }
    }
    break;
  }

  return 0;
}

int usb_close_callback(int user) {

  done = 1;
  return 1;
}

int adapter_send_callback(int user, int transfered) {

  if (transfered < 0) {
    done = 1;
    return 1;
  }

  return 0;
}

int adapter_close_callback(int user) {

  done = 1;
  return 1;
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
  char vendor[128], product[128];
  s_usb_dev * current;
  for (current = usb_devs; current != NULL; ++current) {
    get_vendor_string(vendor, sizeof(vendor), current->vendor_id);
    get_product_string(product, sizeof(product), current->vendor_id, current->product_id);
    printf("%2d", index++);
    printf(" VID 0x%04x (%s)", current->vendor_id, strlen(vendor) ? vendor : "unknown vendor");
    printf(" PID 0x%04x (%s)", current->product_id, strlen(product) ? product : "unknown product");
    printf(" PATH %s\n", current->path);
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

void print_endpoint_properties(uint8_t epProps[ENDPOINT_MAX_NUMBER]) {

  unsigned char i;
  for (i = 0; i < ENDPOINT_MAX_NUMBER; ++i) {
    if (epProps[i] != 0) {
      printf("%hhu", i + 1);
      if (epProps[i] & EP_PROP_IN) {
        printf(" IN");
      }
      if (epProps[i] & EP_PROP_OUT) {
        printf(" OUT");
      }
      if (epProps[i] & EP_PROP_BIDIR) {
        printf(" BIDIR");
      }
      if (epProps[i] & EP_PROP_INT) {
        printf(" INTERRUPT");
      }
      if (epProps[i] & EP_PROP_BLK) {
        printf(" BULK");
      }
      if (epProps[i] & EP_PROP_ISO) {
        printf(" ISOCHRONOUS");
      }
      printf("\n");
    }
  }
}

void get_endpoint_properties(unsigned char configurationIndex, uint8_t epProps[ENDPOINT_MAX_NUMBER]) {

  struct p_configuration * pConfiguration = descriptors->configurations + configurationIndex;
  unsigned char interfaceIndex;
  for (interfaceIndex = 0; interfaceIndex < pConfiguration->descriptor->bNumInterfaces; ++interfaceIndex) {
    struct p_interface * pInterface = pConfiguration->interfaces + interfaceIndex;
    unsigned char altInterfaceIndex;
    for (altInterfaceIndex = 0; altInterfaceIndex < pInterface->bNumAltInterfaces; ++altInterfaceIndex) {
      struct p_altInterface * pAltInterface = pInterface->altInterfaces + altInterfaceIndex;
      unsigned char endpointIndex;
      for (endpointIndex = 0; endpointIndex < pAltInterface->bNumEndpoints; ++endpointIndex) {
        struct usb_endpoint_descriptor * endpoint =
            descriptors->configurations[configurationIndex].interfaces[interfaceIndex].altInterfaces[altInterfaceIndex].endpoints[endpointIndex];
        uint8_t epIndex = ENDPOINT_ADDR_TO_INDEX(endpoint->bEndpointAddress);
        switch (endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) {
        case LIBUSB_TRANSFER_TYPE_INTERRUPT:
          epProps[epIndex] |= EP_PROP_INT;
          break;
        case LIBUSB_TRANSFER_TYPE_BULK:
          epProps[epIndex] |= EP_PROP_BLK;
          break;
        case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
          epProps[epIndex] |= EP_PROP_ISO;
          break;
        }
        if ((endpoint->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
          epProps[epIndex] |= EP_PROP_IN;
        } else {
          epProps[epIndex] |= EP_PROP_OUT;
        }
        if ((epProps[epIndex] & (EP_PROP_IN | EP_PROP_OUT)) == (EP_PROP_IN | EP_PROP_OUT)) {
          epProps[epIndex] |= EP_PROP_BIDIR;
        }
      }
    }
  }
}

int compare_endpoint_properties(uint8_t epPropsSource[ENDPOINT_MAX_NUMBER], uint8_t epPropsTarget[ENDPOINT_MAX_NUMBER]) {

  unsigned char i;
  for (i = 0; i < ENDPOINT_MAX_NUMBER; ++i) {
    if (epPropsSource[i] != 0) {
      if ((epPropsTarget[i] & epPropsSource[i]) != epPropsSource[i]) {
        return 1;
      }
    }
  }

  return 0;
}

void fix_endpoints() {

  pEndpoints = endpoints;

  unsigned char configurationIndex;
  for (configurationIndex = 0; configurationIndex < descriptors->device.bNumConfigurations; ++configurationIndex) {
    uint8_t sourceProperties[ENDPOINT_MAX_NUMBER] = {};
    get_endpoint_properties(configurationIndex, sourceProperties);
    /*print_endpoint_properties(usedEndpoints);
    print_endpoint_properties(endpointProperties);*/
    int renumber = compare_endpoint_properties(sourceProperties, targetProperties);
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
          if (renumber) {
            ++endpointNumber;
            endpoint->bEndpointAddress = (endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) | endpointNumber;
          }
          printf("    endpoint:");
          printf(" %s", ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) ? "IN" : "OUT");
          printf(" %s",
              (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT ? "INTERRUPT" :
              (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK ? "BULK" :
              (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_ISOC ?
                  "ISOCHRONOUS" : "UNKNOWN");
          printf(" %hu", originalEndpoint & USB_ENDPOINT_NUMBER_MASK);
          if (originalEndpoint != endpoint->bEndpointAddress) {
            printf(KRED" -> %hu"KNRM, endpointNumber);
          }
          printf("\n");
          if ((originalEndpoint & USB_ENDPOINT_NUMBER_MASK) == 0) {
            PRINT_ERROR_OTHER("invalid endpoint number")
            continue;
          }
          if (configurationIndex > 0) {
            continue;
          }
          if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_INT) {
            printf("      endpoint %hu won't be configured (not an INTERRUPT endpoint)\n", endpoint->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
            continue;
          }
          if (endpoint->wMaxPacketSize > MAX_PAYLOAD_SIZE_EP) {
            printf("      endpoint %hu won't be configured (max packet size %hu > %hu)\n", endpoint->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK, endpoint->wMaxPacketSize, MAX_PAYLOAD_SIZE_EP);
            continue;
          }
          if (endpointNumber > MAX_ENDPOINTS) {
            printf("      endpoint %hu won't be configured (endpoint number %hhu > %hhu)\n", endpoint->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK, endpointNumber, MAX_ENDPOINTS);
            continue;
          }
          U2S_ENDPOINT(originalEndpoint) = endpoint->bEndpointAddress;
          S2U_ENDPOINT(endpoint->bEndpointAddress) = originalEndpoint;
          pEndpoints->number = endpoint->bEndpointAddress;
          pEndpoints->type = endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
          pEndpoints->size = endpoint->wMaxPacketSize;
          ++pEndpoints;
        }
      }
    }
  }
}

static int add_descriptor(uint16_t wValue, uint16_t wIndex, uint16_t wLength, void * data) {

  if (pDesc + wLength > desc + MAX_DESCRIPTORS_SIZE || pDescIndex >= descIndex + MAX_DESCRIPTORS) {
    fprintf(stderr, "%s:%d %s: unable to add descriptor wValue=0x%04x wIndex=0x%04x wLength=%u (available=%u)\n",
        __FILE__, __LINE__, __func__, wValue, wIndex, wLength, (unsigned int)(MAX_DESCRIPTORS_SIZE - (pDesc - desc)));
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

  ret = add_descriptor((USB_DT_DEVICE << 8), 0, sizeof(descriptors->device), &descriptors->device);
  if (ret < 0) {
    return -1;
  }

  ret = add_descriptor((USB_DT_STRING << 8), 0, sizeof(descriptors->langId0), &descriptors->langId0);
  if (ret < 0) {
    return -1;
  }

  unsigned int descNumber;
  for(descNumber = 0; descNumber < descriptors->device.bNumConfigurations; ++descNumber) {

    ret = add_descriptor((USB_DT_CONFIG << 8) | descNumber, 0, descriptors->configurations[descNumber].descriptor->wTotalLength, descriptors->configurations[descNumber].raw);
    if (ret < 0) {
      return -1;
    }
  }

  for(descNumber = 0; descNumber < descriptors->nbOthers; ++descNumber) {

    ret = add_descriptor(descriptors->others[descNumber].wValue, descriptors->others[descNumber].wIndex, descriptors->others[descNumber].wLength, descriptors->others[descNumber].data);
    if (ret < 0) {
      return -1;
    }
  }

  ret = adapter_send(adapter, E_TYPE_DESCRIPTORS, desc, pDesc - desc);
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
  for (i = 0; i < sizeof(*serialToUsbEndpoint) / sizeof(**serialToUsbEndpoint) && ret >= 0; ++i) {
    uint8_t endpoint = S2U_ENDPOINT(USB_DIR_IN | i);
    if (endpoint) {
      ret = usbasync_poll(usb, endpoint);
    }
  }
  return ret;
}

static int send_out_packet(s_packet * packet) {

  s_endpointPacket * epPacket = (s_endpointPacket *)packet->value;

  return usbasync_write(usb, S2U_ENDPOINT(epPacket->endpoint), epPacket->data, packet->header.length - 1);
}

static int send_control_packet(s_packet * packet) {

  struct usb_ctrlrequest * setup = (struct usb_ctrlrequest *)packet->value;
  if ((setup->bRequestType & USB_RECIP_MASK) == USB_RECIP_ENDPOINT) {
    if (setup->wIndex != 0) {
      setup->wIndex = S2U_ENDPOINT(setup->wIndex);
    }
  }

  return usbasync_write(usb, 0, packet->value, packet->header.length);
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
    gtimer_close(init_timer);
    init_timer = -1;
    printf("Proxy started successfully. Press ctrl+c to stop it.\n");
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
  case E_TYPE_DEBUG:
    {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      printf("%ld.%06ld debug packet received (size = %d bytes)\n", tv.tv_sec, tv.tv_usec, packet->header.length);
      dump(packet->value, packet->header.length);
    }
    break;
  case E_TYPE_RESET:
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

  fix_endpoints();

  return 0;
}

static int timer_close(int user) {
  done = 1;
  return 1;
}

static int timer_read(int user) {
  /*
   * Returning a non-zero value will make gpoll return,
   * this allows to check the 'done' variable.
   */
  return 1;
}

int proxy_start(char * port) {

  int ret = set_prio();
  if (ret < 0)
  {
    PRINT_ERROR_OTHER("Failed to set process priority!")
    return -1;
  }

  adapter = adapter_open(port, process_packet, adapter_send_callback, adapter_close_callback);

  if(adapter < 0) {
    return -1;
  }

  if (send_descriptors() < 0) {
    return -1;
  }

  init_timer = gtimer_start(0, 1000000, timer_close, timer_close, gpoll_register_fd);
  if (init_timer < 0) {
    return -1;
  }

  ret = usbasync_register(usb, 0, usb_read_callback, usb_write_callback, usb_close_callback, gpoll_register_fd);
  if (ret < 0) {
    return -1;
  }

  int timer = gtimer_start(0, 10000, timer_read, timer_close, gpoll_register_fd);
  if (timer < 0) {
    return -1;
  }

  while (!done) {
    gpoll();
  }

  gtimer_close(timer);
  adapter_send(adapter, E_TYPE_RESET, NULL, 0);
  usbasync_close(usb);

  if (init_timer >= 0) {
    PRINT_ERROR_OTHER("Failed to start the proxy: initialization timeout expired!")
    gtimer_close(init_timer);
    return -1;
  }

  return 0;
}

void proxy_stop() {
  done = 1;
}
