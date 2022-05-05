#include "libusbd.h"

#include <stdlib.h>
#include <string.h>

#include "plat/macos/impl.h"

int libusbd_init(libusbd_ctx_t** pCtxOut)
{
    if (!pCtxOut)
        return LIBUSBD_INVALID_ARGUMENT;

    *pCtxOut = malloc(sizeof(libusbd_ctx_t));
    memset(*pCtxOut, 0, sizeof(**pCtxOut));

    libusbd_macos_init(*pCtxOut);

    return LIBUSBD_SUCCESS;
}

int libusbd_free(libusbd_ctx_t* pCtx)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_free(pCtx);

    free(pCtx);

    return LIBUSBD_SUCCESS;
}

int libusbd_config_finalize(libusbd_ctx_t* pCtx)
{
    return libusbd_macos_config_finalize(pCtx);
}

int libusbd_iface_alloc(libusbd_ctx_t* pCtx, uint8_t* pOut)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_iface_alloc(pCtx);

    // TODO oob
    if (pOut)
        *pOut = pCtx->bNumInterfaces++;

    return LIBUSBD_SUCCESS;
}

int libusbd_iface_finalize(libusbd_ctx_t* pCtx, uint8_t iface_num)
{
    return libusbd_macos_iface_finalize(pCtx, iface_num);
}

int libusbd_iface_standard_desc(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz)
{
    libusbd_macos_iface_standard_desc(pCtx, iface_num, descType, unk, pDesc, descSz); // TODO error check
    return LIBUSBD_SUCCESS;
}

int libusbd_iface_nonstandard_desc(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz)
{
    libusbd_macos_iface_nonstandard_desc(pCtx, iface_num, descType, unk, pDesc, descSz); // TODO error check
    return LIBUSBD_SUCCESS;
}

int libusbd_iface_add_endpoint(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t type, uint8_t direction, uint32_t maxPktSize, uint8_t interval, uint64_t unk, uint64_t* pEpOut)
{
    libusbd_macos_iface_add_endpoint(pCtx, iface_num, type, direction, maxPktSize, interval, unk, pEpOut);
    return LIBUSBD_SUCCESS;
}

int libusbd_iface_set_class(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    // TODO oob
    libusbd_iface_t* pIface = &pCtx->aInterfaces[iface_num];

    pIface->bClass = val;
    libusbd_macos_iface_set_class(pCtx, iface_num, val);

    return LIBUSBD_SUCCESS;
}

int libusbd_iface_set_subclass(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    // TODO oob
    libusbd_iface_t* pIface = &pCtx->aInterfaces[iface_num];

    pIface->bSubclass = val;
    libusbd_macos_iface_set_subclass(pCtx, iface_num, val);

    return LIBUSBD_SUCCESS;
}

int libusbd_iface_set_protocol(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    // TODO oob
    libusbd_iface_t* pIface = &pCtx->aInterfaces[iface_num];

    pIface->bProtocol = val;
    libusbd_macos_iface_set_protocol(pCtx, iface_num, val);

    return LIBUSBD_SUCCESS;
}

int libusbd_ep_read(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep, void* data, uint32_t len, uint64_t timeoutMs)
{
    return LIBUSBD_NOT_IMPLEMENTED;
}

int libusbd_ep_write(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep, void* data, uint32_t len, uint64_t timeoutMs)
{
    return libusbd_macos_ep_write(pCtx, iface_num, ep, data, len, timeoutMs);
}