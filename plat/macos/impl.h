#ifndef _LIBUSBD_PLAT_MACOS_IMPL_H
#define _LIBUSBD_PLAT_MACOS_IMPL_H

#include "../../libusbd.h"

typedef struct libusbd_macos_ctx_t libusbd_macos_ctx_t;

int libusbd_macos_init(libusbd_ctx_t* pCtx);
int libusbd_macos_free(libusbd_ctx_t* pCtx);

int libusbd_macos_config_finalize(libusbd_ctx_t* pCtx);

int libusbd_macos_iface_alloc(libusbd_ctx_t* pCtx);
int libusbd_macos_iface_finalize(libusbd_ctx_t* pCtx, uint8_t iface_num);
int libusbd_macos_iface_standard_desc(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz);
int libusbd_macos_iface_nonstandard_desc(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz);
int libusbd_macos_iface_add_endpoint(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t type, uint8_t direction, uint32_t maxPktSize, uint8_t interval, uint64_t unk, uint64_t* pEpOut);
int libusbd_macos_iface_set_class(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val);
int libusbd_macos_iface_set_subclass(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val);
int libusbd_macos_iface_set_protocol(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val);

int libusbd_macos_ep_write(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep, void* data, uint32_t len, uint64_t timeoutMs);

#endif // _LIBUSBD_PLAT_MACOS_IMPL_H