#include <usbasync.h>
#include <serialasync.h>
#include <protocol.h>
#include <stdio.h>
#include <string.h>
#include <GE.h>

#define MAX_SERIAL_PACKET_SIZE 256

#define PRINT_ERROR_OTHER(msg) fprintf(stderr, "%s:%d %s: %s\n", __FILE__, __LINE__, __func__, msg);

static int send_data(int serial, unsigned char type, const unsigned char * data, unsigned int count) {

  if (count != 0 && data == NULL) {
    PRINT_ERROR_OTHER("data is NULL")
    return -1;
  }

  do {
  
    unsigned char length = 254;
    if(count < length) {
    
      length = count;
    }
    struct PACKED {
      unsigned char type;
      unsigned char length;
      unsigned char value[MAX_SERIAL_PACKET_SIZE - 2];
    } packet = { .type = type, .length = length };
    if (data) {
      memcpy(packet.value, data, length);
    }
    data += length;
    count -= length;

    int ret = serialasync_write_timeout(serial, &packet, 2 + length, 1);
    if(ret < 0) {
      return -1;
    }
  } while (count > 0);
  return 0;
}

int usb_read_callback(int user, unsigned char endpoint, const void * buf, unsigned int count) {

  //TODO MLA
  
  return 0;
}

int usb_write_callback(int user, unsigned char endpoint, int transfered) {

  //TODO MLA
  
  return 0;
}

int usb_close_callback(int user) {

  //TODO MLA
  
  return 0;
}

int serial_read_callback(int user, const void * buf, unsigned int count) {

  //TODO MLA

  return 0;
}

int serial_write_callback(int user, int transfered) {

  //TODO MLA

  return 0;
}

int serial_close_callback(int user) {

  //TODO MLA

  return 0;
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

int proxy_init(int usb, int serial) {

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
    unsigned char endpointNumber = 1;
    struct p_configuration * pConfiguration = descriptors->configurations + configurationIndex;
    unsigned char interfaceIndex;
    for (interfaceIndex = 0; interfaceIndex < pConfiguration->descriptor->bNumInterfaces; ++interfaceIndex) {
      struct p_interface * pInterface = pConfiguration->interfaces + interfaceIndex;
      unsigned char altInterfaceIndex;
      for (altInterfaceIndex = 0; altInterfaceIndex < pInterface->bNumAltInterfaces; ++altInterfaceIndex) {
        struct p_altInterface * pAltInterface = pInterface->altInterfaces + altInterfaceIndex;
        unsigned char endpointIndex;
        for (endpointIndex = 0; endpointIndex < pAltInterface->bNumEndpoints; ++endpointIndex) {
          if (endpointNumber > MAX_ENDPOINTS) {
            PRINT_ERROR_OTHER("too many endpoints")
            return -1;
          }
          struct usb_endpoint_descriptor * endpoint =
              descriptors->configurations[configurationIndex].interfaces[interfaceIndex].altInterfaces[altInterfaceIndex].endpoints[endpointIndex];
          // renumber all endpoints
          endpoint->bEndpointAddress = (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) | endpointNumber;
          ++endpointNumber;
          if (endpoint->wMaxPacketSize > MAX_PAYLOAD_SIZE_EP) {
            PRINT_ERROR_OTHER("invalid endpoint size")
          } else if (configurationIndex == 0) {
            pEndpoints->number = endpoint->bEndpointAddress;
            pEndpoints->type = endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
            pEndpoints->size = endpoint->wMaxPacketSize;
            ++pEndpoints;
          }
        }
      }
    }
  }
  
  printf("modified configurations:\n");

  usbasync_print_descriptors(usb);

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
  
  int ret = send_data(serial, E_TYPE_DESCRIPTORS, desc, pDesc - desc);
  if (ret < 0) {
    return -1;
  }
  
  ret = send_data(serial, E_TYPE_INDEX, (unsigned char *)&descIndex, (pDescIndex - descIndex) * sizeof(*descIndex));
  if (ret < 0) {
    return -1;
  }
  
  ret = send_data(serial, E_TYPE_ENDPOINTS, (unsigned char *)&endpoints, (pEndpoints - endpoints) * sizeof(*endpoints));
  if (ret < 0) {
    return -1;
  }

  ret = usbasync_register(usb, 0, usb_read_callback, usb_write_callback, usb_close_callback, GE_AddSource);
  if (ret < 0) {
    return -1;
  }

  ret = serialasync_register(serial, 0, serial_read_callback, serial_write_callback, serial_close_callback, GE_AddSource);
  if (ret < 0) {
    return -1;
  }
  
  //TODO MLA: poll 1st IN endpoint

  return 0;
}

int proxy_stop(int serial) {

  return send_data(serial, E_TYPE_RESET, NULL, 0);
}

