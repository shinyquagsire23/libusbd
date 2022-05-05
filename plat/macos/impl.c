#include "impl.h"

#include <stdlib.h>
#include <string.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach.h>
//#include "IOUSBDeviceControllerLib.h"
#include "alt_IOUSBDeviceControllerLib.h"

typedef struct libusbd_macos_buffer_t
{
    void* data;
    uint64_t size;
    uint64_t token;
} libusbd_macos_buffer_t;

typedef struct libusbd_macos_iface_t
{
    char* pName;
    io_service_t service;
    io_connect_t port;
    libusbd_macos_buffer_t buffer;
} libusbd_macos_iface_t;

typedef struct libusbd_macos_ctx_t
{
    io_service_t service_usbgadget;
    io_connect_t port_usbgadget;
    int configId;
    uint32_t iface_rand32;

    alt_IOUSBDeviceControllerRef controller;
    alt_IOUSBDeviceDescriptionRef desc;

    libusbd_macos_iface_t aInterfaces[16];
    
} libusbd_macos_ctx_t;

CFStringRef serial = CFSTR("Look, Custom Serial");

static uint32_t rng_prev = 0;
static uint32_t stupid_random_u32() {
    return rng_prev*1664525U + 1013904223U; // assuming complement-2 integers and non-signaling overflow
}

kern_return_t IOUSBDeviceInterface_Open(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num)
{
    uint64_t args[1] = { 0 };

    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    return IOConnectCallScalarMethod(pIface->port, 0, args, 1, NULL, NULL);
}

kern_return_t IOUSBDeviceInterface_Close(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    return IOConnectCallScalarMethod(pIface->port, 1, NULL, 0, NULL, NULL);
}

kern_return_t IOUSBDeviceInterface_SetClass(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint8_t val)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    uint64_t args[2] = {val, pImplCtx->configId};
    return IOConnectCallScalarMethod(pIface->port, 3, args, 2, NULL, NULL);
}

kern_return_t IOUSBDeviceInterface_SetSubClass(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint8_t val)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    uint64_t args[2] = {val, pImplCtx->configId};
    return IOConnectCallScalarMethod(pIface->port, 4, args, 2, NULL, NULL);
}

kern_return_t IOUSBDeviceInterface_SetProtocol(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint8_t val)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    uint64_t args[2] = {val, pImplCtx->configId};
    return IOConnectCallScalarMethod(pIface->port, 5, args, 2, NULL, NULL);
}

kern_return_t IOUSBDeviceInterface_CreatePipe(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint8_t type, uint8_t direction, uint32_t maxPktSize, uint8_t interval, uint64_t unk, uint64_t* pPipeOut)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    uint64_t outputCount = 1;
    uint64_t output[1] = {0};
    uint64_t args[6] = {type, direction, maxPktSize, interval, unk, pImplCtx->configId};

    kern_return_t ret = IOConnectCallScalarMethod(pIface->port, 10, args, 6, output, &outputCount);

    if (pPipeOut)
        *pPipeOut = output[0];

    return ret;
}

kern_return_t IOUSBDeviceInterface_AppendStandardClassOrVendorDescriptor(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    uint64_t outputCount = 1;
    uint64_t output[1] = {0};
    uint64_t args[2] = {descType, unk};

    kern_return_t ret = IOConnectCallMethod(pIface->port, 6, args, 2, pDesc, descSz, NULL, NULL, NULL, NULL);

    return ret;
}

kern_return_t IOUSBDeviceInterface_AppendNonstandardClassOrVendorDescriptor(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    uint64_t outputCount = 1;
    uint64_t output[1] = {0};
    uint64_t args[2] = {descType, unk};

    kern_return_t ret = IOConnectCallMethod(pIface->port, 7, args, 2, pDesc, descSz, NULL, NULL, NULL, NULL);

    return ret;
}

kern_return_t IOUSBDeviceInterface_CommitConfiguration(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    kern_return_t ret = IOConnectCallScalarMethod(pIface->port, 11, NULL, 0, NULL, NULL);

    return ret;
}

kern_return_t IOUSBDeviceInterface_CreateBuffer(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint32_t bufferSz, libusbd_macos_buffer_t* pOut)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    uint64_t outputCount = 3;
    uint64_t output[3] = {0};
    uint64_t args[1] = {bufferSz};

    kern_return_t ret = IOConnectCallScalarMethod(pIface->port, 18, args, 1, output, &outputCount);

    if (pOut) {
        pOut->data = (void*)output[0];
        pOut->size = output[1];
        pOut->token = output[2];
    }

    return ret;
}

kern_return_t IOUSBDeviceInterface_ReleaseBuffer(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, libusbd_macos_buffer_t* pBuffer)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    uint64_t args[1] = {pBuffer->token};

    kern_return_t ret = IOConnectCallScalarMethod(pIface->port, 19, args, 1, NULL, NULL);

    return ret;
}

kern_return_t IOUSBDeviceInterface_WritePipe(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint64_t pipe_id, void* data, uint32_t len, uint64_t timeoutMs)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    libusbd_macos_buffer_t* pBuffer = &pIface->buffer;
    uint64_t args[4] = {pipe_id, pBuffer->token, len, timeoutMs};
    uint64_t outputCount = 1;
    uint64_t output[1];

    memcpy(pBuffer->data, data, len); // TODO len check

    kern_return_t ret = IOConnectCallScalarMethod(pIface->port, 14, args, 4, output, &outputCount);

    return ret;
}

int libusbd_macos_init(libusbd_ctx_t* pCtx)
{
    if (!pCtx)
        return LIBUSBD_INVALID_ARGUMENT;

    pCtx->pMacosCtx = malloc(sizeof(libusbd_macos_ctx_t));
    memset(pCtx->pMacosCtx, 0, sizeof(*pCtx->pMacosCtx));

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    // TODO: A lot more error checking
    io_iterator_t       iter    = 0;
    kern_return_t s_ret;
    IOReturn ret;
    kern_return_t open_ret;

    // Seed the RNG
    rng_prev = ((intptr_t)(pImplCtx))>>16;

    pImplCtx->iface_rand32 = stupid_random_u32();

    
    //IOUSBDeviceControllerSendCommand(controller, CFSTR("ShowDeviceDebug"), NULL); 

    for (int i = 0; i < 5; i++)
    {
        CFMutableDictionaryRef match = IOServiceMatching("gay_bowser_usbgadget");
        CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        //CFDictionarySetValue(dict, CFSTR("USBDeviceFunction"), CFSTR("MyUSBData"));
        CFDictionarySetValue(match, CFSTR("IOPropertyMatch"), dict);

        CFRetain(match);
        ret = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
        if (ret != KERN_SUCCESS || iter == 0) {
            printf("Error matching gay_bowser_usbgadget (%x)...\n", ret);
            sleep(1);
            continue;
        }

        // Get the third port
        while (1)
        {
            pImplCtx->service_usbgadget = IOIteratorNext(iter);
            if (!pImplCtx->service_usbgadget) break;

            io_string_t path;
            if (IORegistryEntryGetPath(pImplCtx->service_usbgadget, kIOServicePlane, path) != KERN_SUCCESS) {
                IOObjectRelease(pImplCtx->service_usbgadget);
                continue;
            }

            if (strstr(path, "usb-drd2")) {
                printf("Connecting to: '%s'\n", path);
                break;
            }

            IOObjectRelease(pImplCtx->service_usbgadget);
        } 

        CFRelease(match);
        IOObjectRelease(iter);

        if (pImplCtx->service_usbgadget) break;
    }

    if (!pImplCtx->service_usbgadget) {
        printf("Failed to find gay_bowser_usbgadget, aborting...\n");
        return LIBUSBD_NONDESCRIPT_ERROR;
    }

    alt_IOUSBDeviceControllerCreateFromService(kCFAllocatorDefault, pImplCtx->service_usbgadget, &pImplCtx->controller);

    pImplCtx->desc = alt_IOUSBDeviceDescriptionCreate(kCFAllocatorDefault);//alt_IOUSBDeviceDescriptionCreateFromController(kCFAllocatorDefault, controller);
    //alt_IOUSBDeviceDescriptionSetSerialString(pImplCtx->desc, serial);
    //alt_IOUSBDeviceDescriptionSetProductID(pImplCtx->desc, 0x1234);

    alt_IOUSBDeviceDescriptionRemoveAllConfigurations(pImplCtx->desc);

    pImplCtx->configId = alt_IOUSBDeviceDescriptionAppendConfiguration(pImplCtx->desc, CFSTR("libusbd"), 0, 0);
    

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_free(libusbd_ctx_t* pCtx)
{
    kern_return_t s_ret;

    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    // setDesc
    /*outputCount = 0;
    output[0] = some_str;
    args[0] = some_str; // idk
    s_ret = IOConnectCallMethod(pIface->port, 2, NULL, 0, args, 1, NULL, NULL, NULL, NULL);
    printf("setdesc s_ret %x %x (%x %x)\n", s_ret, outputCount, output[0], output[1]);*/

    for (int j = 0; j < pCtx->bNumInterfaces; j++)
    {
        libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[j];

        s_ret = IOUSBDeviceInterface_ReleaseBuffer(pImplCtx, j, &pIface->buffer);
        printf("release s_ret %x\n", s_ret);

        s_ret = IOUSBDeviceInterface_Close(pImplCtx, j);
        printf("close s_ret %x\n", s_ret);

        IOServiceClose(pIface->port);

        free(pIface->pName);
        pIface->pName = NULL;
    }

    free(pImplCtx);
    pCtx->pMacosCtx = NULL;

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_config_finalize(libusbd_ctx_t* pCtx)
{
    IOReturn ret;
    kern_return_t s_ret;
    io_iterator_t iter = 0;
    kern_return_t open_ret;
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    if (alt_IOUSBDeviceControllerSetDescription(pImplCtx->controller, pImplCtx->desc)) {
        return LIBUSBD_NONDESCRIPT_ERROR;
    }

    //printf("%x\n", IORegistryEntrySetCFProperty(pImplCtx->service_usbgadget, CFSTR("AAAA"), CFSTR("BBBB")));

    //IOUSBDeviceControllerSetPreferredConfiguration(controller, pImplCtx->configId);

    if (IORegistryEntrySetCFProperty(pImplCtx->service_usbgadget, CFSTR("Poke"), CFSTR("Poke"))) {
        return LIBUSBD_NONDESCRIPT_ERROR;
    }


    // Poke usbgadget
    //outputCount = 0;
    //s_ret = IOConnectCallScalarMethod(port_usbgadget, 0, args, 0, output, &outputCount);
    //printf("poke s_ret %x %x (%llx %llx)\n", s_ret, outputCount, output[0], output[1]);

    //IOServiceClose(port_usbgadget);
    IOObjectRelease(pImplCtx->service_usbgadget);

    alt_IOUSBDeviceControllerRelease(pImplCtx->controller);
    CFAllocatorDeallocate(kCFAllocatorDefault, pImplCtx->controller);
    pImplCtx->controller = NULL;

    alt_IOUSBDeviceDescriptionRelease(pImplCtx->desc);
    CFAllocatorDeallocate(kCFAllocatorDefault, pImplCtx->desc);
    pImplCtx->desc = NULL;

    for (int j = 0; j < pCtx->bNumInterfaces; j++)
    {
        libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[j];

        for (int i = 0; i < 10; i++)
        {
            CFStringRef name = CFStringCreateWithCString(NULL, pIface->pName, 0);
            CFMutableDictionaryRef match = IOServiceMatching("IOUSBDeviceInterface");
            CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFDictionarySetValue(dict, CFSTR("USBDeviceFunction"), name);
            CFDictionarySetValue(match, CFSTR("IOPropertyMatch"), dict);

            CFRetain(match);
            ret = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
            if (ret != KERN_SUCCESS || iter == 0) {
                printf("Error matching IOUSBDeviceInterface (%x)...\n", ret);
                sleep(1);
                continue;
            }

            // Get the third port
            while (1)
            {
                pIface->service = IOIteratorNext(iter);
                if (!pIface->service) break;

                io_string_t path;
                if (IORegistryEntryGetPath(pIface->service, kIOServicePlane, path) != KERN_SUCCESS) {
                    IOObjectRelease(pIface->service);
                    continue;
                }

                if (strstr(path, "usb-drd2")) {
                    printf("Connecting to: '%s'\n", path);
                    break;
                }
                IOObjectRelease(pIface->service);
            } 

            CFRelease(match);
            IOObjectRelease(iter);

            if (pIface->service) break;
        }

        if (!pIface->service) {
            printf("Failed to find IOUSBDeviceInterface, aborting...\n");
            return LIBUSBD_NONDESCRIPT_ERROR;
        }

        open_ret = IOServiceOpen(pIface->service, mach_task_self(), 123, &pIface->port);
        printf("open_ret %x %x\n", open_ret, pIface->port);
        //IOServiceClose(pIface->service);
        IOObjectRelease(pIface->service);

        
        // Open
        s_ret = IOUSBDeviceInterface_Open(pImplCtx, j);
        printf("open s_ret %x\n", s_ret);

        s_ret = IOUSBDeviceInterface_CreateBuffer(pImplCtx, j, 0x1000, &pIface->buffer);
        printf("createData s_ret %x\n", s_ret);

    }

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_finalize(libusbd_ctx_t* pCtx, uint8_t iface_num)
{
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_CommitConfiguration(pImplCtx, iface_num);
    return LIBUSBD_SUCCESS;
}


int libusbd_macos_iface_standard_desc(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz)
{
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_AppendStandardClassOrVendorDescriptor(pImplCtx, iface_num, descType, unk, pDesc, descSz); // TODO error check
    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_nonstandard_desc(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz)
{
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_AppendNonstandardClassOrVendorDescriptor(pImplCtx, iface_num, descType, unk, pDesc, descSz); // TODO error check
    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_add_endpoint(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t type, uint8_t direction, uint32_t maxPktSize, uint8_t interval, uint64_t unk, uint64_t* pEpOut)
{
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_CreatePipe(pImplCtx, iface_num, type, direction, maxPktSize, interval, unk, pEpOut);

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_alloc(libusbd_ctx_t* pCtx)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    // TODO oob
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[pCtx->bNumInterfaces];

    // We have to include a random element, otherwise we might connect to a port which is about to
    // be replaced.
    pIface->pName = malloc(0x80);
    snprintf(pIface->pName, 0x80, "iface-%08x-%u", pImplCtx->iface_rand32, pCtx->bNumInterfaces);
    CFStringRef name = CFStringCreateWithCString(NULL, pIface->pName, 0);

    alt_IOUSBDeviceDescriptionAppendInterfaceToConfiguration(pImplCtx->desc, pImplCtx->configId, name);

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_set_class(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_SetClass(pImplCtx, iface_num, val); // TODO error check

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_set_subclass(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_SetSubClass(pImplCtx, iface_num, val); // TODO error check

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_set_protocol(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_SetProtocol(pImplCtx, iface_num, val); // TODO error check

    return LIBUSBD_SUCCESS;
}


int libusbd_macos_ep_write(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep, void* data, uint32_t len, uint64_t timeoutMs)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    kern_return_t ret = IOUSBDeviceInterface_WritePipe(pImplCtx, iface_num, ep, data, len, timeoutMs);
    if (ret == 0xE0000001)
    {
        return LIBUSBD_NOT_ENUMERATED;
    }
    else if (ret == 0xE00002D6)
    {
        return LIBUSBD_TIMEOUT;
    }
    else if (ret & 0xFFF00000)
    {
        printf("Unknown error from IOUSBDeviceInterface_WritePipe: %08x\n", ret);
        return LIBUSBD_NONDESCRIPT_ERROR;
    }

    return ret;
}