#ifndef _LIBUSBD_PLAT_LINUX_IMPL_PRIV_H
#define _LIBUSBD_PLAT_LINUX_IMPL_PRIV_H

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
} libusbd_linux_iface_t;

typedef struct libusbd_linux_ctx_t
{
    int configId;
    uint32_t iface_rand32;

    libusbd_linux_iface_t aInterfaces[16];
    
} libusbd_linux_ctx_t;

#endif // _LIBUSBD_PLAT_LINUX_IMPL_PRIV_H
