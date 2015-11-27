#include <usbasync.h>
#include <serialasync.h>
#include <protocol.h>
#include <stdio.h>
#include <string.h>

#define MAX_SERIAL_PACKET_SIZE 256

#define PRINT_ERROR_OTHER(msg) fprintf(stderr, "%s:%d %s: %s\n", __FILE__, __LINE__, __func__, msg);

static struct {
  struct {
    struct {
      struct {
        struct {
          unsigned char number;
          unsigned char type;
        } * endpoints;
        unsigned char nbEndpoints;
      } * altInterfaces;
      unsigned char nbAltInterfaces;
      unsigned char currentAltInterface;
    } * interfaces;
    unsigned char nbInterfaces;
    unsigned char currentInterface;
  } * configurations;
  unsigned char nbConfigurations;
  unsigned char currentConfiguration;
} device;

static struct {
  unsigned char number;
  unsigned char type;
} endpointCapabilities[] = {
  {1, LIBUSB_TRANSFER_TYPE_INTERRUPT},
  {2, LIBUSB_TRANSFER_TYPE_INTERRUPT},
  {3, LIBUSB_TRANSFER_TYPE_INTERRUPT},
  {4, LIBUSB_TRANSFER_TYPE_INTERRUPT},
  {5, LIBUSB_TRANSFER_TYPE_INTERRUPT},
  {6, LIBUSB_TRANSFER_TYPE_INTERRUPT},
  {7, 0},
  {8, 0},
  {9, 0},
  {10, 0},
  {11, 0},
  {12, 0},
  {13, 0},
  {14, 0},
  {15, 0},
};

static int send_data(int serial, unsigned char type, const unsigned char * data, unsigned int count) {

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
    memcpy(packet.value, data, length);
    data += length;
    count -= length;

    int ret = serialasync_write_timeout(serial, &packet, 2 + length, 1);
    if(ret < 0) {
      return -1;
    }
  } while (count > 0);
  return 0;
}

#define ADD_DESCRIPTOR(WVALUE,NUMBER,WLENGTH,DATA) \
  if (pDesc + WLENGTH <= desc + MAX_DESCRIPTORS_SIZE && pDescIndex < descIndex + MAX_DESCRIPTORS) { \
    pDescIndex->offset = pDesc - desc; \
    pDescIndex->wValue = WVALUE; \
    pDescIndex->wIndex = NUMBER; \
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
  
  descriptors->device.bMaxPacketSize0 = 64;

  s_endpoint endpoints[MAX_ENDPOINTS] = {};
  s_endpoint * pEndpoints = endpoints;
  unsigned char endpointNumbers[LIBUSB_ENDPOINT_DIR_MASK];
  
  unsigned char configurationIndex;
  for (configurationIndex = 0; configurationIndex < descriptors->device.bNumConfigurations; ++configurationIndex) {
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
          printf("    endpoint: 0x%02x\n", pAltInterface->endpoints[endpointIndex]->bEndpointAddress);
        }
      }
    }
  }
  
  unsigned int endpointIndex;
  for (endpointIndex = 0; endpointIndex < descriptors->configurations[0].interfaces[0].altInterfaces[0].bNumEndpoints; ++endpointIndex) {
    struct usb_endpoint_descriptor * endpoint = descriptors->configurations[0].interfaces[0].altInterfaces[0].endpoints[endpointIndex];
    //TODO MLA: modify endpoint numbers in all configs, and make an s_endpoint table for config#0.itf#0
    //TODO MLA: try not to modify if possible
    //dans chaque interface il faut chercher tous les types pour chaque endpoint
    //puis chercher un endpoint qui supporte tous les types, de manière minimale
  }

  //unsigned int size = 0;
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

  return 0;
}

int proxy_stop(int serial) {

  return send_data(serial, E_TYPE_RESET, NULL, 0);
}

