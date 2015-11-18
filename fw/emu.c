/*
 * Copyright 2015  Mathieu Laurendeau (mat.lau [at] laposte [dot] net)
 * License: GPLv3
 */
 
#include "emu.h"

#include <LUFA/Drivers/Peripheral/Serial.h>
#include "../protocol.h"

#define LED_CONFIG  (DDRD |= (1<<6))
#define LED_OFF   (PORTD &= ~(1<<6))
#define LED_ON    (PORTD |= (1<<6))

#define MAX_CONTROL_TRANSFER_SIZE 64
#define MAX_EP_SIZE 64

#define USART_BAUDRATE 500000
#define USART_DOUBLE_SPEED false

/*
 * This is a block of memory to store all descriptors.
 */
static uint8_t descriptors[1024]; // there should not be more than 254 descriptors
static struct __attribute__((packed)) {
    uint8_t type;
    uint8_t number;
    uint8_t offset;
    uint16_t size;
} descIndex[32]; // 32 x 5 = 160 bytes (should not exceed 254 bytes)
static struct __attribute__((packed)) {
    uint8_t number; // 0 means end of table
    uint8_t type;
    uint8_t size;
} endpoints[16]; // 48 bytes

static uint8_t * pdesc = descriptors;

static struct __attribute__((packed)) {
    uint8_t endpoint; // 0 means nothing to send
    uint8_t size;
    uint8_t data[MAX_EP_SIZE];
} input; // 66 bytes (should not exceed 255 bytes)

static uint8_t * pdata;
static unsigned char i = 0;

/*
 * These variables are used in both the main and serial interrupt,
 * therefore they have to be declared as volatile.
 */
static volatile unsigned char started = 0;
static volatile unsigned char packet_type = 0;
static volatile unsigned char value_len = 0;
static volatile unsigned char controlReply = 0;
static volatile unsigned char controlReplyLen = 0;

void forceHardReset(void) {

    LED_ON;
    cli(); // disable interrupts
    wdt_enable(WDTO_15MS); // enable watchdog
    while(1); // wait for watchdog to reset processor
}

static inline int8_t Serial_BlockingReceiveByte(void) {

    while(!Serial_IsCharReceived()) {}
    return UDR1;
}

static inline void send_control_header(void) {

    uint8_t len = sizeof(USB_ControlRequest);
    if( !(USB_ControlRequest.bmRequestType & REQDIR_DEVICETOHOST) ) {
        len += (USB_ControlRequest.wLength & 0xFF);
    }
    Serial_SendByte(BYTE_CONTROL);
    Serial_SendByte(len);
    Serial_SendData(&USB_ControlRequest, sizeof(USB_ControlRequest));
}

static inline void ack(uint8_t type) {

    Serial_SendByte(type);
    Serial_SendByte(BYTE_LEN_0_BYTE);
}

static inline void handle_packet(void) {

    switch (packet_type) {
    case BYTE_DESCRIPTORS:
        pdesc += value_len;
        ack(packet_type);
        break;
    case BYTE_ENDPOINTS:
        ack(packet_type);
        break;
    case BYTE_START:
        ack(packet_type);
        started = 1;
        break;
    case BYTE_CONTROL:
        controlReply = 1;
        controlReplyLen = value_len;
        break;
    case BYTE_IN:
        break;
    case BYTE_RESET:
        forceHardReset();
        break;
    }
}

static unsigned char buf[MAX_CONTROL_TRANSFER_SIZE];

ISR(USART1_RX_vect) {

    packet_type = UDR1;
    value_len = Serial_BlockingReceiveByte();
    // start with highest priority types
    if (packet_type == BYTE_CONTROL) {
        pdata = buf;
    }
    else if (packet_type == BYTE_IN) {
        pdata = (uint8_t*)&input;
    }
    else if (packet_type == BYTE_ENDPOINTS) {
        pdata = (uint8_t*)&endpoints;
    }
    else if (packet_type == BYTE_INDEX) {
        pdata = (uint8_t*)&descIndex;
    }
    else if (packet_type == BYTE_DESCRIPTORS) {
        pdata = pdesc;
    }
    while (i < value_len) {
        pdata[i++] = Serial_BlockingReceiveByte();
    }
    i = 0;
    handle_packet();
}

void serial_init(void) {

    Serial_Init(USART_BAUDRATE, USART_DOUBLE_SPEED);

    UCSR1B |= (1 << RXCIE1); // Enable the USART Receive Complete interrupt (USART_RXC)
}

void SetupHardware(void) {

    /* Disable watchdog */
    MCUSR = 0;
    wdt_disable();

    clock_prescale_set(clock_div_1);

    serial_init();

    GlobalInterruptEnable();

    LED_CONFIG;

    LEDs_Init();

    while(!started) {}

    USB_Init();
}

void EVENT_USB_Device_Connect(void) {

}

void EVENT_USB_Device_Disconnect(void) {

}

void EVENT_USB_Device_ConfigurationChanged(void) {

    uint8_t i;
    for (i = 0; i < sizeof(endpoints) / sizeof(*endpoints) && endpoints[i].number; ++i) {
        if(endpoints[i].type == EP_TYPE_INTERRUPT) {
            Endpoint_ConfigureEndpoint(endpoints[i].number, endpoints[i].type, endpoints[i].size, 1);
        }
        //TODO MLA: other endpoint types
    }
}

uint16_t CALLBACK_USB_GetDescriptor(const uint16_t wValue, const uint8_t wIndex,
        const void** const DescriptorAddress) {

    const uint8_t DescriptorType = (wValue >> 8);
    const uint8_t DescriptorNumber = (wValue & 0xFF);

    uint8_t i;
    for (i = 0; i < sizeof(descIndex) / sizeof(*descIndex); ++i) {
        if(DescriptorType == descIndex[i].type && DescriptorNumber == descIndex[i].number) {
            *DescriptorAddress = descriptors + descIndex[i].offset;
            return descIndex[i].size;
        }
    }

    return 0;
}

void EVENT_USB_Device_ControlRequest(void) {

    //TODO MLA: handle interface requests here
}

void EVENT_USB_Device_UnhandledControlRequest(void) {

    if(USB_ControlRequest.bmRequestType | REQDIR_DEVICETOHOST) {
        controlReply = 0;
        send_control_header();
        while(!controlReply);
        Endpoint_ClearSETUP();
        Endpoint_Write_Control_Stream_LE(buf, controlReplyLen);
        Endpoint_ClearOUT();
    } else {
        static unsigned char buffer[MAX_CONTROL_TRANSFER_SIZE];
        Endpoint_ClearSETUP();
        Endpoint_Read_Control_Stream_LE(buffer, USB_ControlRequest.wLength);
        Endpoint_ClearIN();
        send_control_header();
        Serial_SendData(buffer, USB_ControlRequest.wLength);
    }
}

void SendNextInput(void) {

    if (input.endpoint) {

        Endpoint_SelectEndpoint(input.endpoint);

        while (!Endpoint_IsINReady()) {}

        Endpoint_Write_Stream_LE(input.data, input.size, NULL);

        Endpoint_ClearIN();

        input.endpoint = 0;

        ack(BYTE_IN);
    }
}

void ReceiveNextOutput(void) {

    uint8_t endpoint = 0; //TODO MLA

    if(endpoint) {

        Endpoint_SelectEndpoint(endpoint);

        if (Endpoint_IsOUTReceived()) {

            static struct {
                struct {
                    unsigned char type;
                    unsigned char length;
                } header;
                unsigned char buffer[MAX_EP_SIZE];
            } packet = { .header.type = BYTE_OUT };

            uint16_t length = 0;

            if (Endpoint_IsReadWriteAllowed()) {
                uint8_t ErrorCode = Endpoint_Read_Stream_LE(packet.buffer, sizeof(packet.buffer), &length);
                if(ErrorCode == ENDPOINT_RWSTREAM_NoError) {
                    length = sizeof(packet.buffer);
                }
            }

            Endpoint_ClearOUT();

            if(length) {
                packet.header.length = length & 0xFF;
                Serial_SendData(&packet, sizeof(packet.header) + packet.header.length);
            }
        }
    }
}

void ENDPOINT_Task(void) {

    if (USB_DeviceState != DEVICE_STATE_Configured) {
        return;
    }

    SendNextInput();

    ReceiveNextOutput();
}

int main(void) {

    SetupHardware();

    for (;;) {
        ENDPOINT_Task();
        USB_USBTask();
    }
}
