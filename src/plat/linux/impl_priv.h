#ifndef _LIBUSBD_PLAT_LINUX_IMPL_PRIV_H
#define _LIBUSBD_PLAT_LINUX_IMPL_PRIV_H

#include <linux/usb/functionfs.h>

typedef struct libusbd_linux_writeheader_t
{
    struct usb_functionfs_descs_head_v2 header;
    __le32 fs_count;
    __le32 hs_count;
    __le32 ss_count;
} libusbd_linux_writeheader_t;

typedef struct libusbd_linux_buffer_t
{
    void* data;
    uint64_t size;
    uint64_t token;
} libusbd_linux_buffer_t;

typedef struct libusbd_linux_ep_t
{
    uint64_t last_transferred;
    uint64_t ep_async_done;
    int32_t last_error;
    uint64_t maxPktSize;

    libusbd_linux_buffer_t buffer;
} libusbd_linux_ep_t;

typedef struct libusbd_linux_iface_t
{
    char* pName;

    libusbd_linux_buffer_t setup_buffer;

    libusbd_setup_callback_t setup_callback;
    libusbd_setup_callback_info_t setup_callback_info;
    int class_async_done;

    int is_builtin;

    uint8_t bNumEndpoints;
    libusbd_linux_ep_t aEndpoints[16];

    struct usb_interface_descriptor descFFS;
    struct usb_endpoint_descriptor_no_audio aEndpointsFFS[16];

} libusbd_linux_iface_t;

typedef struct libusbd_linux_ctx_t
{
    int configId;
    uint32_t iface_rand32;

    libusbd_linux_iface_t aInterfaces[16];

    union
    {
        void* write_descs;
        libusbd_linux_writeheader_t* write_header;
    };

    void* write_descs_next;
    uint32_t write_descs_sz;

    int ep0_fd;
    
} libusbd_linux_ctx_t;

#endif // _LIBUSBD_PLAT_LINUX_IMPL_PRIV_H
