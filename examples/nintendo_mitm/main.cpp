#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <time.h>
#include <errno.h>  

#include <libusbd.h>
#include <libusb.h>

#include "usb_hid_keys.h"
#include "HIDReportData.h"

#include <chrono>
#include <ctime>
#include <cstring>
#include <hidapi.h>

#define VID_NINTENDO (0x057e)

#define JOYCON_L_BT (0x2006)
#define JOYCON_R_BT (0x2007)
#define PRO_CONTROLLER_EARLY (0x2009)
#define PRO_CONTROLLER (0x2019)
#define JOYCON_CHARGING_GRIP (0x200e)
#define N64_CONTROLLER (0x2017)

#define NS2_JOYCON_R (0x2066)
#define NS2_JOYCON_L (0x2067)
#define NS2_JOYCON_CHARGING_GRIP_HUB (0x2068)
#define NS2_PRO_CONTROLLER (0x2069)
#define NS2_GAMECUBE_CONTROLLER (0x2073)
//unsigned short product_ids_size = 6;
//unsigned short product_ids[] = {JOYCON_L_BT, JOYCON_R_BT, PRO_CONTROLLER_EARLY, PRO_CONTROLLER, JOYCON_CHARGING_GRIP, N64_CONTROLLER};

unsigned short product_ids_size = 4;
unsigned short product_ids[] = {NS2_JOYCON_R, NS2_JOYCON_L, NS2_PRO_CONTROLLER, NS2_GAMECUBE_CONTROLLER};

// Uncomment for spam
#define DEBUG_PRINT

bool bluetooth = false;
uint8_t global_count = 0;

uint8_t hid_rd_buf[0x200];
uint8_t hid_wr_buf[0x200];
uint8_t sidechannel_rd_buf[0x200];

typedef struct {
    uint8_t in_ep;
    uint8_t out_ep;
} interface_endpoints;

typedef struct {
    libusb_context *usbctx;
    libusb_device_handle *usbhandle;
    hid_device *hid_handle;
    interface_endpoints sidechannel;
} controller_ctx;

controller_ctx cctx;


int find_endpoints(const struct libusb_interface_descriptor *iface_desc, interface_endpoints *eps) {
    eps->in_ep = 0;
    eps->out_ep = 0;

    for (int i = 0; i < iface_desc->bNumEndpoints; i++) {
        const struct libusb_endpoint_descriptor *ep = &iface_desc->endpoint[i];
        if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK) {
            if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN && eps->in_ep == 0)
                eps->in_ep = ep->bEndpointAddress;
            else if (!(ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) && eps->out_ep == 0)
                eps->out_ep = ep->bEndpointAddress;
        }
    }

    return (eps->in_ep && eps->out_ep) ? 0 : -1;
}

void hex_dump(const unsigned char *buf, int len)
{
    for (int i = 0; i < len; i++)
        printf("%02x ", buf[i]);
    printf("\n");
}

static int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

volatile sig_atomic_t stop;

void inthand(int signum) {
    stop = 1;
}

int ns2_send(controller_ctx *pCCtx, const uint8_t* buf, int len) {
    int res = 0;
    int transferred = 0;
    
    if (!pCCtx) {
        return -1;
    }

#if 0
        printf("(MITM) TX to sidechannel EP 0x%x (0x%x bytes, res=%d): ", pCCtx->sidechannel.out_ep, len, res);
        hex_dump(buf, len);
#endif

#if 0
    if (len > 64) {
        for (int i = 0; i < len; i += 64) {
            res = ns2_send(pCCtx, buf + i, len % 64);
            if (res) break;
        }
        return res;
    }
#endif

    res = libusb_bulk_transfer(pCCtx->usbhandle, pCCtx->sidechannel.out_ep, (uint8_t*)buf, len, &transferred, 1000); // 1-second timeout
    if (res == 0 && transferred == len) {
        //printf("(MITM) TX'd %d bytes successfully\n", transferred);
    } else {
        printf("(MITM) TX error: %s (sent %d bytes)\n", libusb_error_name(res), transferred);
    }

#ifdef DEBUG_PRINT
        printf("(MITM) TX to sidechannel EP 0x%x (0x%x bytes, res=%d): ", pCCtx->sidechannel.out_ep, transferred, res);
        if (!transferred) {
            printf("(none)");
        }
        hex_dump(buf, transferred);
#endif

    return 0;
}

int ns2_read(controller_ctx *pCCtx, uint8_t* buf) {
    int res = 0;
    int transferred = 0;
    
    if (!pCCtx) {
        return -1;
    }

    res = libusb_bulk_transfer(pCCtx->usbhandle, pCCtx->sidechannel.in_ep, (uint8_t*)buf, 0x200, &transferred, 10); // 10ms timeout
    if (res == 0) {
        //printf("Read %d bytes successfully\n", transferred);
    }
    else if (res == LIBUSB_ERROR_TIMEOUT) {

    } 
    else {
        printf("(MITM) RX error: %s (sent %d bytes)\n", libusb_error_name(res), transferred);
    }

#ifdef DEBUG_PRINT
        printf("(MITM) RX from sidechannel EP 0x%x (0x%x bytes, res=%d): ", pCCtx->sidechannel.in_ep, transferred, res);
        if (!transferred) {
            printf("(none)");
        }
        hex_dump(buf, transferred);
#endif

    return transferred;
}

int ns2_init(controller_ctx *pCCtx)
{
    unsigned char buf[0x400];
    unsigned char sn_buffer[14] = {0x00};
    memset(buf, 0, 0x400);
    int res = 0;
    int transferred = 0;

    if (!pCCtx) {
        return -1;
    }
    
    if(!bluetooth)
    {
        // Get MAC Left
        memset(buf, 0x00, 0x400);
        unsigned char req_1[] = {0x3, 0x91, 0x0, 0xd, 0x0, 0x8, 0x0, 0x0, 0x1, 0x0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        memcpy(buf, req_1, sizeof(req_1));
        //hid_exchange(handle_side, buf, sizeof(req_1));

        ns2_send(pCCtx, req_1, sizeof(req_1));

        // Do handshaking
        memset(buf, 0x00, 0x400);
        unsigned char req_2[] = {0x9, 0x91, 0x0, 0x7, 0x0, 0x8, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
        memcpy(buf, req_2, sizeof(req_2));
        //hid_exchange(handle_side, buf, sizeof(req_2));

        ns2_send(pCCtx, req_2, sizeof(req_2));
    }

#if 0
    // Enable vibration
    memset(buf, 0x00, 0x400);
    buf[0] = 0x01; // Enabled
    joycon_send_subcommand(handle, 0x1, 0x48, buf, 1);
    
    // Enable IMU data
    memset(buf, 0x00, 0x400);
    buf[0] = 0x01; // Enabled
    joycon_send_subcommand(handle, 0x1, 0x40, buf, 1);
    
    // Increase data rate for Bluetooth
    if (bluetooth)
    {
       memset(buf, 0x00, 0x400);
       buf[0] = 0x31; // Enabled
       joycon_send_subcommand(handle, 0x1, 0x3, buf, 1);
    }
    
    //Read device's S/N
    spi_read(handle, 0x6002, sn_buffer, 0xE);
    
    printf("Successfully initialized %ls with S/N: %c%c%c%c%c%c%c%c%c%c%c%c%c%c!\n", 
        name, sn_buffer[0], sn_buffer[1], sn_buffer[2], sn_buffer[3], 
        sn_buffer[4], sn_buffer[5], sn_buffer[6], sn_buffer[7], sn_buffer[8], 
        sn_buffer[9], sn_buffer[10], sn_buffer[11], sn_buffer[12], 
        sn_buffer[13]);
#endif

    return 0;
}

void device_print(struct hid_device_info *dev)
{
    printf("USB device info:\n  vid: 0x%04hX pid: 0x%04hX\n  path: %s\n  MAC: %ls\n  interface_number: %d\n",
        dev->vendor_id, dev->product_id, dev->path, dev->serial_number, dev->interface_number);
    printf("  Manufacturer: %ls\n", dev->manufacturer_string);
    printf("  Product:      %ls\n\n", dev->product_string);
}

/*
Nintendo does this weird song and dance:

Iface A setup callback! bmRequestType=c0 bRequest=03 wValue=0000 wIndex=0000 wLength=0040
Iface B setup callback! bmRequestType=c0 bRequest=03 wValue=0000 wIndex=0000 wLength=0040
Iface B setup callback! bmRequestType=c0 bRequest=02 wValue=0000 wIndex=0000 wLength=0010
Iface A setup callback! bmRequestType=c0 bRequest=02 wValue=0000 wIndex=0000 wLength=0010
Iface B setup callback! bmRequestType=40 bRequest=04 wValue=0276 wIndex=0000 wLength=0000
Iface A setup callback! bmRequestType=40 bRequest=04 wValue=0276 wIndex=0000 wLength=0000

*/

//uint16_t pid_override = 0x2079;

uint8_t silly_intercept[] = {0x01, 0x00, 0x48, 0x45, 0x57, 0x37, 0x30, 0x30, 0x30, 0x33, 0x37, 0x35, 0x34, 0x38, 0x34, 0x32, 0x00, 0x00, 0x7e, 0x05, 0x69, 0x20, 0x01, 0x06, 0x01, 0x23, 0x23, 0x23, 0xa0, 0xa0, 0xa0, 0xe6, 0xe6, 0xe6, 0x32, 0x32, 0x32, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; // procon
//uint8_t silly_intercept[] = {0x01, 0x00, 0x48, 0x43, 0x57, 0x35, 0x31, 0x30, 0x30, 0x36, 0x32, 0x37, 0x38, 0x39, 0x38, 0x36, 0x00, 0x00, 0x7e, 0x05, 0x66, 0x20, 0x01, 0x08, 0x02, 0x32, 0x32, 0x32, 0xaa, 0xaa, 0xaa, 0xff, 0x8c, 0x5f, 0x32, 0x32, 0x32, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; // joycon r
//uint8_t silly_intercept[] = {0x01, 0x00, 0x48, 0x48, 0x57, 0x35, 0x30, 0x30, 0x30, 0x32, 0x31, 0x31, 0x37, 0x36, 0x34, 0x30, 0x00, 0x00, 0x7e, 0x05, 0x73, 0x20, 0x01, 0x04, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; // gc controller
//uint8_t silly_intercept[] = {0x01, 0x00, 0x48, 0x43, 0x57, 0x35, 0x31, 0x30, 0x30, 0x36, 0x32, 0x37, 0x38, 0x39, 0x38, 0x36, 0x00, 0x00, 0x7e, 0x05, 0x66, 0x20, 0x01, 0x08, 0x02, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; // joycon r clown colors
//uint8_t silly_intercept[] = {0x01, 0x00, 0x48, 0x45, 0x57, 0x37, 0x30, 0x30, 0x30, 0x33, 0x37, 0x35, 0x34, 0x38, 0x34, 0x32, 0x00, 0x00, 0x7e, 0x05, (uint8_t)(pid_override & 0xFF), (uint8_t)((pid_override >> 8) & 0xFF), 0x01, 0x06, 0x02, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; // procon clown colors

uint16_t pid_override = 0;

int iface_a_setup_callback(libusbd_setup_callback_info_t* info)
{
    printf("(USBD) Iface A setup callback! bmRequestType=%02x bRequest=%02x wValue=%04x wIndex=%04x wLength=%04x\n", info->bmRequestType, info->bRequest, info->wValue, info->wIndex, info->wLength);
    //hex_dump(info->out_data)

    if (info->bmRequestType == 0xC0 && info->bRequest == 0x03 && false) {
        memcpy(info->out_data, silly_intercept, sizeof(silly_intercept));
        info->out_len = info->wLength;
    }
    else {
        libusb_control_transfer(cctx.usbhandle, info->bmRequestType, info->bRequest, info->wValue, info->wIndex, (uint8_t*)info->out_data, info->wLength, 1000);
        info->out_len = info->wLength;
    }


#ifdef DEBUG_PRINT
    printf("(MITM) Iface A setup response: ");
    hex_dump((const uint8_t*)info->out_data, info->out_len);
#endif

    return 0;
}

int iface_b_setup_callback(libusbd_setup_callback_info_t* info)
{
    printf("(USBD) Iface B setup callback! bmRequestType=%02x bRequest=%02x wValue=%04x wIndex=%04x wLength=%04x\n", info->bmRequestType, info->bRequest, info->wValue, info->wIndex, info->wLength);

    //libusb_control_transfer(cctx.usbhandle, info->bmRequestType, info->bRequest, info->wValue, info->wIndex, (uint8_t*)info->out_data, info->wLength, 1000);
    //info->out_len = info->wLength;

    if (info->bmRequestType == 0xC0 && info->bRequest == 0x03 && false) {
        memcpy(info->out_data, silly_intercept, sizeof(silly_intercept));
        info->out_len = info->wLength;
    }
    else {
        libusb_control_transfer(cctx.usbhandle, info->bmRequestType, info->bRequest, info->wValue, info->wIndex, (uint8_t*)info->out_data, info->wLength, 1000);
        info->out_len = info->wLength;
    }

#ifdef DEBUG_PRINT
    printf("(MITM) Iface B setup response: ");
    hex_dump((const uint8_t*)info->out_data, info->out_len);
#endif

    return 0;
}


int main(int argc, char* argv[])
{
    libusb_device **devs;
    ssize_t cnt;
    int res;
    unsigned char buf[2][0x400] = {0};
    uint16_t selected_pid = 0;
    libusbd_ctx_t* pCtx;
    struct hid_device_info *hid_devs, *hid_dev_iter;

    setbuf(stdout, NULL); // turn off stdout buffering for test reasons
    signal(SIGINT, inthand);

    if ((res = libusb_init(&cctx.usbctx)) < 0) {
        fprintf(stderr, "libusb init error: %s\n", libusb_error_name(res));
        return EXIT_FAILURE;
    }

    res = hid_init();
    if(res) {
        printf("Failed to open hid library! Exiting...\n");
        return -1;
    }

    cnt = libusb_get_device_list(cctx.usbctx, &devs);
    for (size_t pid_idx = 0; pid_idx < product_ids_size; pid_idx++) {
        uint16_t possible_pid = product_ids[pid_idx];
        for (ssize_t i = 0; i < cnt; i++) {
            struct libusb_device_descriptor desc;
            libusb_get_device_descriptor(devs[i], &desc);

            if (desc.idVendor == VID_NINTENDO && desc.idProduct == possible_pid) {
                selected_pid = possible_pid;
                break;
            }
        }

        if (selected_pid) {
            break;
        }
    }

    if (!selected_pid) {
        printf("Couldn't find any valid Nintendo Switch 2 controllers.\n");
        return -1;
    }
    

    for (ssize_t i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(devs[i], &desc);

        if (desc.idVendor == VID_NINTENDO && desc.idProduct == selected_pid) {
            res = libusb_open(devs[i], &cctx.usbhandle);
            if (res != 0 || !cctx.usbhandle) {
                fprintf(stderr, "Failed to open device: %s\n", libusb_error_name(res));
                continue;
            }

            int iface = 1;
            res = libusb_claim_interface(cctx.usbhandle, iface);
            if (res != 0) {
                fprintf(stderr, "Failed to claim interface %d: %s\n", iface, libusb_error_name(res));
                continue;
            }

            struct libusb_config_descriptor *config;
            libusb_get_active_config_descriptor(devs[i], &config);
            const struct libusb_interface_descriptor *iface_desc = &config->interface[iface].altsetting[0];

            
            if (find_endpoints(iface_desc, &cctx.sidechannel) == 0) {
                printf("Interface %d: IN=0x%02X OUT=0x%02X\n", iface, cctx.sidechannel.in_ep, cctx.sidechannel.out_ep);
                // You can now use eps.in_ep and eps.out_ep with libusb_bulk_transfer.
            } else {
                fprintf(stderr, "Could not find both IN/OUT endpoints on interface %d\n", iface);
            }

            libusb_free_config_descriptor(config);

            hid_devs = hid_enumerate(VID_NINTENDO, selected_pid);
            hid_dev_iter = hid_devs;
            while(hid_dev_iter)
            {
                // Sometimes hid_enumerate still returns other product IDs
                if (hid_dev_iter->product_id != selected_pid) break;
                
                device_print(hid_dev_iter);
                
                // on windows this will be -1 for devices with one interface
                if(hid_dev_iter->interface_number == 0 || hid_dev_iter->interface_number == -1)
                {
                    cctx.hid_handle = hid_open_path(hid_dev_iter->path);
                    if(cctx.hid_handle == NULL)
                    {
                        printf("Failed to open controller at %ls, continuing...\n", hid_dev_iter->path);
                        hid_dev_iter = hid_dev_iter->next;
                        continue;
                    }

                    printf("Initialized HID interface.\n");

                    break;
                }
                
                hid_dev_iter = hid_dev_iter->next;
            }
            hid_free_enumeration(hid_devs);

            break;  // Stop after finding the first matching device
        }
    }

    libusb_free_device_list(devs, 1);

    printf("Initializing controller?\n");
    //ns2_init(&cctx);
    printf("Initialized controller.");
    
    // Get hid desc
    uint8_t test_buf[0x200];
    //libusb_control_transfer(cctx.usbhandle, 0x81, 0x6, 0x2100, 0, test_buf, 0xFF, 1000);
    /*for (int i = 0; i < 0xFF; i++) {
        libusb_control_transfer(cctx.usbhandle, 0x80, 0x6, 0x0300 | i, 0x0409, test_buf, 0xFE, 1000);
    }*/

    // Start setting up our spoofing MiTM device
    libusbd_init(&pCtx);

    if (pid_override) {
        selected_pid = pid_override;
    }

    libusbd_set_vid(pCtx, VID_NINTENDO);
    libusbd_set_pid(pCtx, selected_pid);
    libusbd_set_version(pCtx, 0x0101);

    libusbd_set_class(pCtx, 0xEF); // Miscellaneous 
    libusbd_set_subclass(pCtx, 2);
    libusbd_set_protocol(pCtx, 1);

    libusbd_set_manufacturer_str(pCtx, "Nintendo");
    libusbd_set_product_str(pCtx, "Nintendo GameCube Controller");
    libusbd_set_serial_str(pCtx,  "00");

    uint8_t iface_a_num = 0;
    uint64_t iface_a_ep_out;
    uint64_t iface_a_ep_in;
    libusbd_iface_alloc(pCtx, &iface_a_num);

    uint8_t iface_b_num = 0;
    uint64_t iface_b_ep_out;
    uint64_t iface_b_ep_in;
    libusbd_iface_alloc(pCtx, &iface_b_num);

    libusbd_config_finalize(pCtx);

    libusbd_iface_set_class(pCtx, iface_a_num, 3); // HID
    libusbd_iface_set_subclass(pCtx, iface_a_num, 0);
    libusbd_iface_set_protocol(pCtx, iface_a_num, 0);

    // TODO fetch these dynamically?
    uint8_t hid_report_desc[] = {0x5, 0x1, 0x9, 0x5, 0xa1, 0x1, 0x85, 0x5, 0x5, 0xff, 0x9, 0x1, 0x15, 0x0, 0x26, 0xff, 0x0, 0x95, 0x3f, 0x75, 0x8, 0x81, 0x2, 0x85, 0xa, 0x9, 0x1, 0x95, 0x2, 0x81, 0x2, 0x5, 0x9, 0x19, 0x1, 0x29, 0x15, 0x25, 0x1, 0x95, 0x15, 0x75, 0x1, 0x81, 0x2, 0x95, 0x1, 0x75, 0x3, 0x81, 0x3, 0x5, 0x1, 0x9, 0x1, 0xa1, 0x0, 0x9, 0x30, 0x9, 0x31, 0x9, 0x33, 0x9, 0x35, 0x26, 0xff, 0xf, 0x95, 0x4, 0x75, 0xc, 0x81, 0x2, 0xc0, 0x5, 0xff, 0x9, 0x2, 0x26, 0xff, 0x0, 0x95, 0x34, 0x75, 0x8, 0x81, 0x2, 0x85, 0x3, 0x9, 0x1, 0x95, 0x3f, 0x91, 0x2, 0xc0};
    uint8_t hid_desc[] = {0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(hid_report_desc), 0x00};

    libusbd_iface_standard_desc(pCtx, iface_a_num, 0x21, 0xF, hid_desc, sizeof(hid_desc));
    libusbd_iface_nonstandard_desc(pCtx, iface_a_num, 0x22, 0xF, hid_report_desc, sizeof(hid_report_desc));
    libusbd_iface_add_endpoint(pCtx, iface_a_num, USB_EPATTR_TTYPE_INTR, USB_EP_DIR_IN, 64, 4, 0, &iface_a_ep_out);
    libusbd_iface_add_endpoint(pCtx, iface_a_num, USB_EPATTR_TTYPE_INTR, USB_EP_DIR_OUT, 64, 4, 0, &iface_a_ep_in);
    
    libusbd_iface_set_class_cmd_callback(pCtx, iface_a_num, iface_a_setup_callback);

    libusbd_iface_finalize(pCtx, iface_a_num);
    
    libusbd_iface_set_class(pCtx, iface_b_num, 0xFF); // Vendor Specific
    libusbd_iface_set_subclass(pCtx, iface_b_num, 0);
    libusbd_iface_set_protocol(pCtx, iface_b_num, 0);

    libusbd_iface_add_endpoint(pCtx, iface_b_num, USB_EPATTR_TTYPE_BULK, USB_EP_DIR_OUT, 64, 1, 0, &iface_b_ep_in);
    libusbd_iface_add_endpoint(pCtx, iface_b_num, USB_EPATTR_TTYPE_BULK, USB_EP_DIR_IN, 64, 1, 0, &iface_b_ep_out);
    
    libusbd_iface_set_class_cmd_callback(pCtx, iface_b_num, iface_b_setup_callback);

    libusbd_iface_finalize(pCtx, iface_b_num);

    printf("iface_a ep_out %llx ep_in %llx\n", iface_a_ep_out, iface_a_ep_in);

restart_loop:
    int idx = 0;
    while (!stop) {
        res = libusbd_ep_read_start(pCtx, iface_a_num, iface_a_ep_in, 64, 10);
        if (res == LIBUSBD_NOT_ENUMERATED) {
            char speen_chars[4] = {'/', '-', '\\', '|'};
            printf("\rWaiting for device enumeration...  %c\r", speen_chars[idx % 4]);

            idx++;
            msleep(1);
        }
        else if (!res) {
            break;
        }
        else {
            printf("res %x\n", res);
        }
    }
    if (stop) {
        return -1;
    }
    
    res = libusbd_ep_read_start(pCtx, iface_b_num, iface_b_ep_in, 512, 10);
    printf("res %x\n", res);

    hid_set_nonblocking(cctx.hid_handle, 1);

    printf("Start main loop.\n");
    
    while (!stop) {
        //LIBUSBD_TIMEOUT
        res = libusbd_ep_transfer_done(pCtx, iface_a_num, iface_a_ep_in);
        if (res) {
            int transferred = libusbd_ep_transferred_bytes(pCtx, iface_a_num, iface_a_ep_in);
            uint8_t* readBuf = NULL;
            res = libusbd_ep_get_buffer(pCtx, iface_a_num, iface_a_ep_in, (void**)&readBuf);
            if (res && readBuf) {
                printf("(USBD) RX from HID EP 0x%x (0x%x bytes, res=%d): ", iface_a_ep_in, transferred, res);
                hex_dump(readBuf, transferred);
            }
            else {
                printf("No buffer for A %x %p\n", res, readBuf);
            }

            // TODO hid_write

            res = libusbd_ep_read_start(pCtx, iface_a_num, iface_a_ep_in, 64, 10);
            if (res == LIBUSBD_NOT_ENUMERATED) {
                goto restart_loop;
            }
        }

        res = libusbd_ep_transfer_done(pCtx, iface_b_num, iface_b_ep_in);
        if (res) {
            int transferred = libusbd_ep_transferred_bytes(pCtx, iface_b_num, iface_b_ep_in);
            uint8_t* readBuf = NULL;
            res = libusbd_ep_get_buffer(pCtx, iface_b_num, iface_b_ep_in, (void**)&readBuf);
            if (res && readBuf) {
                printf("(USBD) RX from sidechannel EP 0x%x (0x%x bytes, res=%d): ", iface_a_ep_in, transferred, res);
                hex_dump(readBuf, transferred);
            }
            else {
                printf("No buffer for B %x %p\n", res, readBuf);
            }

            // TODO libusb_bulk_write

            ns2_send(&cctx, readBuf, transferred); // TODO error check

            res = libusbd_ep_read_start(pCtx, iface_b_num, iface_b_ep_in, 512, 10);
            if (res == LIBUSBD_NOT_ENUMERATED) {
                goto restart_loop;
            }
        }

        res = hid_read(cctx.hid_handle, hid_rd_buf, 0x400);
        if(res > 0) {
#ifdef DEBUG_PRINT
            printf("(MITM) RX from HID EP 0x%x: ", iface_a_ep_out);
            hex_dump(hid_rd_buf, res);
#endif

#ifdef DEBUG_PRINT
            //printf("(USBD) TX to HID EP 0x%x: ", iface_a_ep_out);
            //hex_dump(hid_rd_buf, res);
#endif
            memcpy(hid_wr_buf, hid_rd_buf, res);
            //if (libusbd_ep_transfer_done(pCtx, iface_a_num, iface_a_ep_out)) {
            libusbd_ep_write_start(pCtx, iface_a_num, iface_a_ep_out, hid_wr_buf, res, 10);
            //}
        }

        int bytes_read = ns2_read(&cctx, sidechannel_rd_buf);
        if (bytes_read) {
            libusbd_ep_write_start(pCtx, iface_b_num, iface_b_ep_out, sidechannel_rd_buf, bytes_read, 10);
        }

        msleep(10);
        idx++;

        char speen_chars[4] = {'/', '-', '\\', '|'};
        printf("\rSpeen %c\r", speen_chars[idx % 4]);
    }

    hid_close(cctx.hid_handle);

    // Finalize the hidapi library
    res = hid_exit();

    libusbd_free(pCtx);

    if (cctx.usbhandle) {
        libusb_close(cctx.usbhandle);
    }
    libusb_exit(cctx.usbctx);

    return 0;
}
