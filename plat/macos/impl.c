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

typedef struct libusbd_macos_ctx_t
{
    io_service_t service_interface;
    io_service_t service_usbgadget;
    io_connect_t port_interface;
    io_connect_t port_usbgadget;
    int configId;

    libusbd_macos_buffer_t buffer;
    
} libusbd_macos_ctx_t;

CFStringRef serial = CFSTR("Look, Custom Serial");

kern_return_t IOUSBDeviceInterface_Open(libusbd_macos_ctx_t* pImplCtx)
{
    uint64_t args[1] = { 0 };

    return IOConnectCallScalarMethod(pImplCtx->port_interface, 0, args, 1, NULL, NULL);
}

kern_return_t IOUSBDeviceInterface_Close(libusbd_macos_ctx_t* pImplCtx)
{
    return IOConnectCallScalarMethod(pImplCtx->port_interface, 1, NULL, 0, NULL, NULL);
}

kern_return_t IOUSBDeviceInterface_SetClass(libusbd_macos_ctx_t* pImplCtx, uint8_t val)
{
    uint64_t args[2] = {val, pImplCtx->configId};
    return IOConnectCallScalarMethod(pImplCtx->port_interface, 3, args, 2, NULL, NULL);
}

kern_return_t IOUSBDeviceInterface_SetSubClass(libusbd_macos_ctx_t* pImplCtx, uint8_t val)
{
    uint64_t args[2] = {val, pImplCtx->configId};
    return IOConnectCallScalarMethod(pImplCtx->port_interface, 4, args, 2, NULL, NULL);
}

kern_return_t IOUSBDeviceInterface_SetProtocol(libusbd_macos_ctx_t* pImplCtx, uint8_t val)
{
    uint64_t args[2] = {val, pImplCtx->configId};
    return IOConnectCallScalarMethod(pImplCtx->port_interface, 5, args, 2, NULL, NULL);
}

kern_return_t IOUSBDeviceInterface_CreatePipe(libusbd_macos_ctx_t* pImplCtx, uint8_t type, uint8_t direction, uint32_t maxPktSize, uint8_t interval, uint64_t unk, uint64_t* pPipeOut)
{
    uint64_t outputCount = 1;
    uint64_t output[1] = {0};
    uint64_t args[6] = {type, direction, maxPktSize, interval, unk, pImplCtx->configId};

    kern_return_t ret = IOConnectCallScalarMethod(pImplCtx->port_interface, 10, args, 6, output, &outputCount);

    if (pPipeOut)
        *pPipeOut = output[0];

    return ret;
}

kern_return_t IOUSBDeviceInterface_AppendStandardClassOrVendorDescriptor(libusbd_macos_ctx_t* pImplCtx, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz)
{
    uint64_t outputCount = 1;
    uint64_t output[1] = {0};
    uint64_t args[2] = {descType, unk};

    kern_return_t ret = IOConnectCallMethod(pImplCtx->port_interface, 6, args, 2, pDesc, descSz, NULL, NULL, NULL, NULL);

    return ret;
}

kern_return_t IOUSBDeviceInterface_AppendNonstandardClassOrVendorDescriptor(libusbd_macos_ctx_t* pImplCtx, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz)
{
    uint64_t outputCount = 1;
    uint64_t output[1] = {0};
    uint64_t args[2] = {descType, unk};

    kern_return_t ret = IOConnectCallMethod(pImplCtx->port_interface, 7, args, 2, pDesc, descSz, NULL, NULL, NULL, NULL);

    return ret;
}

kern_return_t IOUSBDeviceInterface_CommitConfiguration(libusbd_macos_ctx_t* pImplCtx)
{
    kern_return_t ret = IOConnectCallScalarMethod(pImplCtx->port_interface, 11, NULL, 0, NULL, NULL);

    return ret;
}

kern_return_t IOUSBDeviceInterface_CreateBuffer(libusbd_macos_ctx_t* pImplCtx, uint32_t bufferSz, libusbd_macos_buffer_t* pOut)
{
    uint64_t outputCount = 3;
    uint64_t output[3] = {0};
    uint64_t args[1] = {bufferSz};

    kern_return_t ret = IOConnectCallScalarMethod(pImplCtx->port_interface, 18, args, 1, output, &outputCount);

    if (pOut) {
        pOut->data = (void*)output[0];
        pOut->size = output[1];
        pOut->token = output[2];
    }

    return ret;
}

kern_return_t IOUSBDeviceInterface_ReleaseBuffer(libusbd_macos_ctx_t* pImplCtx, libusbd_macos_buffer_t* pBuffer)
{
    uint64_t args[1] = {pBuffer->token};

    kern_return_t ret = IOConnectCallScalarMethod(pImplCtx->port_interface, 19, args, 1, NULL, NULL);

    return ret;
}

kern_return_t IOUSBDeviceInterface_WritePipe(libusbd_macos_ctx_t* pImplCtx, uint64_t pipe_id, void* data, uint32_t len, uint64_t timeoutMs)
{
    libusbd_macos_buffer_t* pBuffer = &pImplCtx->buffer;
    uint64_t args[4] = {pipe_id, pBuffer->token, len, timeoutMs};
    uint64_t outputCount = 1;
    uint64_t output[1];

    memcpy(pBuffer->data, data, len); // TODO len check

    kern_return_t ret = IOConnectCallScalarMethod(pImplCtx->port_interface, 14, args, 4, output, &outputCount);

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

    uint64_t output[10] = {0};
    uint32_t outputCount = 0;
    uint64_t args[10] = { 0, 0, 0, 0, 0, 0,0,0,0,0 };

    alt_IOUSBDeviceControllerRef controller;

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

    //open_ret = IOServiceOpen(pImplCtx->service_usbgadget, mach_task_self(), 123, &port_usbgadget);
    //printf("open_ret kext %x %x\n", open_ret, port_usbgadget);

    alt_IOUSBDeviceControllerCreateFromService(kCFAllocatorDefault, pImplCtx->service_usbgadget, &controller);
    //int configId = 0;
#if 1
    alt_IOUSBDeviceDescriptionRef desc = alt_IOUSBDeviceDescriptionCreate(kCFAllocatorDefault);//alt_IOUSBDeviceDescriptionCreateFromController(kCFAllocatorDefault, controller);
    alt_IOUSBDeviceDescriptionSetSerialString(desc, serial);

    alt_IOUSBDeviceDescriptionSetProductID(desc, 0x1234);

    alt_IOUSBDeviceDescriptionRemoveAllConfigurations(desc);

    pImplCtx->configId = alt_IOUSBDeviceDescriptionAppendConfiguration(desc, CFSTR("JustAKeyboard"), 0, 0);
    //alt_IOUSBDeviceDescriptionAppendInterfaceToConfiguration(desc, pImplCtx->configId, CFSTR("AppleUSBMux"));
    alt_IOUSBDeviceDescriptionAppendInterfaceToConfiguration(desc, pImplCtx->configId, CFSTR("MyUSBData"));
    //alt_IOUSBDeviceDescriptionAppendInterfaceToConfiguration(desc, pImplCtx->configId, CFSTR("MyUSBControlAux"));
    //alt_IOUSBDeviceDescriptionAppendInterfaceToConfiguration(desc, pImplCtx->configId, CFSTR("MyUSBData"));
    //alt_IOUSBDeviceDescriptionAppendInterfaceToConfiguration(desc, pImplCtx->configId, CFSTR("MyUSBDataAux"));
    //CFDictionarySetValue(desc->info, , kCFBooleanTrue);
#endif

    ret = alt_IOUSBDeviceControllerSetDescription(controller, desc);
    printf("%x: system=%x subsystem=%x code=%x\n", ret, err_get_system(ret), err_get_sub(ret), err_get_code(ret));

    //printf("%x\n", IORegistryEntrySetCFProperty(pImplCtx->service_usbgadget, CFSTR("AAAA"), CFSTR("BBBB")));

    //IOUSBDeviceControllerSetPreferredConfiguration(controller, pImplCtx->configId);

    printf("%x\n", IORegistryEntrySetCFProperty(pImplCtx->service_usbgadget, CFSTR("Poke"), CFSTR("Poke")));


    // Poke usbgadget
    //outputCount = 0;
    //s_ret = IOConnectCallScalarMethod(port_usbgadget, 0, args, 0, output, &outputCount);
    //printf("poke s_ret %x %x (%llx %llx)\n", s_ret, outputCount, output[0], output[1]);

    //IOServiceClose(port_usbgadget);
    IOObjectRelease(pImplCtx->service_usbgadget);

    alt_IOUSBDeviceControllerRelease(controller);
    CFAllocatorDeallocate(kCFAllocatorDefault, controller);

    alt_IOUSBDeviceDescriptionRelease(desc);
    CFAllocatorDeallocate(kCFAllocatorDefault, desc);

    // Wait for the old interface to get nuked
    sleep(1);

    for (int i = 0; i < 10; i++)
    {
        CFMutableDictionaryRef match = IOServiceMatching("IOUSBDeviceInterface");
        CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(dict, CFSTR("USBDeviceFunction"), CFSTR("MyUSBData"));
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
            pImplCtx->service_interface = IOIteratorNext(iter);
            if (!pImplCtx->service_interface) break;

            io_string_t path;
            if (IORegistryEntryGetPath(pImplCtx->service_interface, kIOServicePlane, path) != KERN_SUCCESS) {
                IOObjectRelease(pImplCtx->service_interface);
                continue;
            }

            if (strstr(path, "usb-drd2")) {
                printf("Connecting to: '%s'\n", path);
                break;
            }
            IOObjectRelease(pImplCtx->service_interface);
        } 

        CFRelease(match);
        IOObjectRelease(iter);

        if (pImplCtx->service_interface) break;
    }

    if (!pImplCtx->service_interface) {
        printf("Failed to find IOUSBDeviceInterface, aborting...\n");
        return LIBUSBD_NONDESCRIPT_ERROR;
    }

    open_ret = IOServiceOpen(pImplCtx->service_interface, mach_task_self(), 123, &pImplCtx->port_interface);
    printf("open_ret %x %x\n", open_ret, pImplCtx->port_interface);
    //IOServiceClose(pImplCtx->service_interface);
    IOObjectRelease(pImplCtx->service_interface);

    
    // Open
    s_ret = IOUSBDeviceInterface_Open(pImplCtx);
    printf("open s_ret %x %x (%llx %llx)\n", s_ret, outputCount, output[0], output[1]);

    s_ret = IOUSBDeviceInterface_CreateBuffer(pImplCtx, 0x1000, &pImplCtx->buffer);
    printf("createData s_ret %x\n", s_ret);
    

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_free(libusbd_ctx_t* pCtx)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    uint64_t output[10] = {0};
    uint32_t outputCount = 0;
    uint64_t args[10] = { 0, 0, 0, 0, 0, 0,0,0,0,0 };
    kern_return_t s_ret;

    

    // setDesc
    /*outputCount = 0;
    output[0] = some_str;
    args[0] = some_str; // idk
    s_ret = IOConnectCallMethod(pImplCtx->port_interface, 2, NULL, 0, args, 1, NULL, NULL, NULL, NULL);
    printf("setdesc s_ret %x %x (%x %x)\n", s_ret, outputCount, output[0], output[1]);*/

    s_ret = IOUSBDeviceInterface_ReleaseBuffer(pImplCtx, &pImplCtx->buffer);
    printf("release s_ret %x %x (%llx %llx)\n", s_ret, outputCount, output[0], output[1]);

    s_ret = IOUSBDeviceInterface_Close(pImplCtx);
    printf("close s_ret %x %x (%llx %llx)\n", s_ret, outputCount, output[0], output[1]);

    IOServiceClose(pImplCtx->port_interface);

    free(pImplCtx);
    pCtx->pMacosCtx = NULL;

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_finalize(libusbd_ctx_t* pCtx, uint8_t iface_num)
{
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_CommitConfiguration(pImplCtx);
    return LIBUSBD_SUCCESS;
}


int libusbd_macos_iface_standard_desc(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz)
{
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_AppendStandardClassOrVendorDescriptor(pImplCtx, descType, unk, pDesc, descSz); // TODO error check
    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_nonstandard_desc(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t descType, uint8_t unk, uint8_t* pDesc, uint64_t descSz)
{
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_AppendNonstandardClassOrVendorDescriptor(pImplCtx, descType, unk, pDesc, descSz); // TODO error check
    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_add_endpoint(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t type, uint8_t direction, uint32_t maxPktSize, uint8_t interval, uint64_t unk, uint64_t* pEpOut)
{
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_CreatePipe(pImplCtx, type, direction, maxPktSize, interval, unk, pEpOut);

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_alloc(libusbd_ctx_t* pCtx, uint8_t* pOut)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    // TODO oob
    if (pOut)
        *pOut = pCtx->bNumInterfaces++;

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_set_class(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_SetClass(pImplCtx, val); // TODO error check

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_set_subclass(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_SetSubClass(pImplCtx, val); // TODO error check

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_set_protocol(libusbd_ctx_t* pCtx, uint8_t iface_num, uint8_t val)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_SetProtocol(pImplCtx, val); // TODO error check

    return LIBUSBD_SUCCESS;
}


int libusbd_macos_ep_write(libusbd_ctx_t* pCtx, uint64_t ep, void* data, uint32_t len, uint64_t timeoutMs)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    kern_return_t ret = IOUSBDeviceInterface_WritePipe(pImplCtx, ep, data, len, timeoutMs);
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