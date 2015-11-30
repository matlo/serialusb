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
static int serial = -1;
static int adapter = -1;

#define TRACE printf("%s\n", __func__);

int usb_read_callback(int user, unsigned char endpoint, const void * buf, unsigned int count) {

  TRACE
  //TODO MLA
  
  return 0;
}

int usb_write_callback(int user, unsigned char endpoint, int transfered) {

  TRACE
  //TODO MLA
  
  return 0;
}

int usb_close_callback(int user) {

  TRACE
  //TODO MLA
  
  return 1;
}

static void dump(const unsigned char * packet, unsigned char length) {

  int i;
  for (i = 0; i < length; ++i) {
    if (i && !(i % 8)) {
      printf("\n");
    }
    printf("0x%02x ", packet[i]);
  }
  printf("\n");
}

static int process_packet(int user, s_packet * packet)
{
  unsigned char type = packet->header.type;
  unsigned char length = packet->header.length;
  unsigned char * data = packet->value;

  int ret = 0;

  switch (packet->header.type) {
  /*case E_TYPE_CONTROL:
    //TODO MLA
    break;
  case E_TYPE_OUT:
    //TODO MLA
    break;
  case E_TYPE_DEBUG:
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

  return ret;
}

int serial_read_callback(int user, const void * buf, unsigned int count) {

  printf("%s\n", __func__);

  return adapter_recv(adapter, buf, count);
}

int serial_write_callback(int user, int transfered) {

  if (transfered < 0) {
    return 1;
  }

  return 0;
}

int serial_close_callback(int user) {

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

int proxy_init() {

  char * path = usb_select();

  if(path == NULL) {
    fprintf(stderr, "No USB device selected!\n");
    return -1;
  }

  usb = usbasync_open_path(path);

  free(path);

  if (usb >= 0) {

    const s_usb_descriptors * descriptors = usbasync_get_usb_descriptors(usb);

    printf("Opened device: VID 0x%04x PID 0x%04x PATH %s\n", descriptors->device.idVendor, descriptors->device.idProduct, path);

    printf("original endpoints:\n");

    usbasync_print_endpoints(usb);

    serial = serialasync_open("/dev/ttyUSB0", 500000);

    if (serial >= 0) {

      adapter = adapter_add(serial, process_packet);

      if(adapter >= 0) {

        s_usb_descriptors * descriptors = usbasync_get_usb_descriptors(usb);

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

        descriptors->device.bMaxPacketSize0 = MAX_PACKET_SIZE_EP0;

        s_endpoint endpoints[MAX_ENDPOINTS] = {};
        s_endpoint * pEndpoints = endpoints;

        unsigned char configurationIndex;
        for (configurationIndex = 0; configurationIndex < descriptors->device.bNumConfigurations; ++configurationIndex) {
          unsigned char endpointNumber = 0;
          struct p_configuration * pConfiguration = descriptors->configurations + configurationIndex;
          unsigned char interfaceIndex;
          for (interfaceIndex = 0; interfaceIndex < pConfiguration->descriptor->bNumInterfaces; ++interfaceIndex) {
            struct p_interface * pInterface = pConfiguration->interfaces + interfaceIndex;
            unsigned char altInterfaceIndex;
            for (altInterfaceIndex = 0; altInterfaceIndex < pInterface->bNumAltInterfaces; ++altInterfaceIndex) {
              struct p_altInterface * pAltInterface = pInterface->altInterfaces + altInterfaceIndex;
              unsigned char endpointIndex;
              for (endpointIndex = 0; endpointIndex < pAltInterface->bNumEndpoints; ++endpointIndex) {
                ++endpointNumber;
                struct usb_endpoint_descriptor * endpoint =
                    descriptors->configurations[configurationIndex].interfaces[interfaceIndex].altInterfaces[altInterfaceIndex].endpoints[endpointIndex];
                // renumber all endpoints
                endpoint->bEndpointAddress = (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) | endpointNumber;
                if (configurationIndex > 0) {
                  continue;
                }
                if (endpointNumber > MAX_ENDPOINTS) {
                  printf("endpoint %hu won't be configured (endpoint number %hhu > %hhu)\n", endpoint->bEndpointAddress & LIBUSB_ENDPOINT_ADDRESS_MASK, endpointNumber, MAX_ENDPOINTS);
                  continue;
                }
                if (endpoint->wMaxPacketSize > MAX_PAYLOAD_SIZE_EP) {
                  printf("endpoint %hu won't be configured (max packet size %hu > %hu)\n", endpoint->bEndpointAddress & LIBUSB_ENDPOINT_ADDRESS_MASK, endpoint->wMaxPacketSize, MAX_PAYLOAD_SIZE_EP);
                  continue;
                }
                pEndpoints->number = endpoint->bEndpointAddress;
                pEndpoints->type = endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
                pEndpoints->size = endpoint->wMaxPacketSize;
                ++pEndpoints;
              }
            }
          }
        }

        printf("modified endpoints:\n");

        usbasync_print_endpoints(usb);

        unsigned char warn = 0;

        unsigned char desc[MAX_DESCRIPTORS_SIZE];
        unsigned char * pDesc = desc;
        s_descIndex descIndex[MAX_DESCRIPTORS];
        s_descIndex * pDescIndex = descIndex;

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

        int ret = adapter_send(adapter, E_TYPE_DESCRIPTORS, desc, pDesc - desc, 1);
        if (ret < 0) {
          return -1;
        }

        ret = adapter_send(adapter, E_TYPE_INDEX, (unsigned char *)&descIndex, (pDescIndex - descIndex) * sizeof(*descIndex), 1);
        if (ret < 0) {
          return -1;
        }

        ret = adapter_send(adapter, E_TYPE_ENDPOINTS, (unsigned char *)&endpoints, (pEndpoints - endpoints) * sizeof(*endpoints), 1);
        if (ret < 0) {
          return -1;
        }

        ret = usbasync_register(usb, 0, usb_read_callback, usb_write_callback, usb_close_callback, GE_AddSource);
        if (ret < 0) {
          return -1;
        }

        ret = serialasync_register(serial, adapter, serial_read_callback, serial_write_callback, serial_close_callback, GE_AddSource);
        if (ret < 0) {
          return -1;
        }

        //TODO MLA: poll 1st IN endpoint
      } else {

        fprintf(stderr, "error adding adapter\n");
      }
    } else {

      fprintf(stderr, "error opening serial device\n");
    }
  }

  return 0;
}

int proxy_stop(int serial) {

  adapter_send(serial, E_TYPE_RESET, NULL, 0, 1);
  serialasync_close(serial);
  usbasync_close(usb);

  return 0;
}

