#include "impl.h"

#include <stdlib.h>
#include <string.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach.h>

#include <pthread.h>
//#include "IOUSBDeviceControllerLib.h"
#include "alt_IOUSBDeviceControllerLib.h"

typedef struct libusbd_macos_buffer_t
{
    void* data;
    uint64_t size;
    uint64_t token;
} libusbd_macos_buffer_t;

typedef struct libusbd_macos_ep_t
{
    uint64_t last_read;
    uint64_t ep_async_done;
    libusbd_macos_buffer_t buffer;
    IONotificationPortRef notification_port;
    mach_port_t mnotification_port;
} libusbd_macos_ep_t;

typedef struct libusbd_macos_iface_t
{
    char* pName;
    io_service_t service;
    io_connect_t port;
    
    mach_port_t async_port;
    IONotificationPortRef notification_port;
    mach_port_t mnotification_port;
    libusbd_macos_buffer_t setup_buffer;

    libusbd_setup_callback_t setup_callback;
    libusbd_setup_callback_info_t setup_callback_info;
    int class_async_done;

    uint8_t bNumEndpoints;
    libusbd_macos_ep_t aEndpoints[16];
} libusbd_macos_iface_t;

typedef struct libusbd_macos_ctx_t
{
    io_service_t service_usbgadget;
    io_connect_t port_usbgadget;
    int configId;
    uint32_t iface_rand32;

    alt_IOUSBDeviceControllerRef controller;
    alt_IOUSBDeviceDescriptionRef desc;

    IONotificationPortRef notification_port;
    mach_port_t mnotification_port;

    libusbd_macos_iface_t aInterfaces[16];
    
} libusbd_macos_ctx_t;

CFRunLoopRef _runLoop = NULL;

static uint32_t rng_prev = 0;
static uint32_t stupid_random_u32() {
    return rng_prev*1664525U + 1013904223U; // assuming complement-2 integers and non-signaling overflow
}

mach_port_t create_port()
{
    mach_port_t p = MACH_PORT_NULL;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &p);
    mach_port_insert_right(mach_task_self(), p, p, MACH_MSG_TYPE_MAKE_SEND);
    return p;
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

    pthread_yield_np();

    return res;
}

void* RunLoopThread(void* data)
{
    printf("Start runloop\n");

    _runLoop = CFRunLoopGetCurrent();

    // Start loop
    while (_runLoop)
    {
        CFRunLoopRun();
        msleep(1);
        printf(".\n");
    }

    printf("Stopped runloop\n");

    //Not reached, CFRunLoopRun doesn't return in this case.
    return NULL;
}

int LaunchRunLoopThread()
{
    if (_runLoop!=NULL)
        return 0;

    // Create the thread using POSIX routines.
    pthread_attr_t  attr;
    pthread_t       posixThreadID;
    int             returnVal;

    returnVal = pthread_attr_init(&attr);
    if (returnVal != 0)
        return returnVal;

    returnVal = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (returnVal != 0)
        return returnVal;

    int     threadError = pthread_create(&posixThreadID, &attr, &RunLoopThread, NULL);

    returnVal = pthread_attr_destroy(&attr);
    if (returnVal != 0)
        return returnVal;

    if (threadError != 0)
        return threadError;

    return 0;
}

void StopRunLoopThread()
{
    if (_runLoop!=NULL)
        CFRunLoopStop(_runLoop);
    _runLoop = NULL;
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

kern_return_t IOUSBDeviceInterface_CompleteClassCommandCallback(libusbd_macos_iface_t* pIface, uint8_t iface_num, libusbd_setup_callback_info_t* info, uint64_t* arguments)
{
    uint64_t args[5] = {1,info->out_len,info->out_data,0,arguments[4]};
    kern_return_t ret = IOConnectCallScalarMethod(pIface->port, 9, args, 5, NULL, NULL);

    //printf("complete %x\n", ret);

    return ret;
}

void test_callback(void* refcon, IOReturn result, uint64_t* arguments)
{
    libusbd_macos_iface_t* pIface = (libusbd_macos_iface_t*)refcon;

    pIface->class_async_done = 1;

    // We get two callbacks, the second can be ignored
    //bool is_done = arguments[0];
    //if (is_done) return;

    pIface->setup_callback_info.bmRequestType = (arguments[1] >> 24) & 0xFF;
    pIface->setup_callback_info.bRequest = (arguments[1] >> 16) & 0xFF;
    pIface->setup_callback_info.wValue = (arguments[1]) & 0xFFFF;
    pIface->setup_callback_info.wIndex = (arguments[2] >> 16) & 0xFFFF;
    pIface->setup_callback_info.wLength = (arguments[2]) & 0xFFFF;

    pIface->setup_callback_info.out_len = 0;
    pIface->setup_callback_info.out_data = pIface->setup_buffer.data;

    if (pIface->setup_callback)
        pIface->setup_callback(&pIface->setup_callback_info);
    
    printf("AAAAAAA %x %p, %llx %llx %llx %llx %llx\n", result, arguments, arguments[0], arguments[1], arguments[2], arguments[3], arguments[4]);

    IOUSBDeviceInterface_CompleteClassCommandCallback(pIface, 0, &pIface->setup_callback_info, arguments); // TODO bad
    printf("BBBBBBB\n");
}

kern_return_t IOUSBDeviceInterface_SetClassCommandCallbacks(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, bool a, bool b, bool c)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];
    uint64_t args[3] = {a,b,c};

    uint64_t asyncRef[8];
    asyncRef[kIOAsyncReservedIndex] = pIface->mnotification_port;
    asyncRef[kIOAsyncCalloutFuncIndex] = test_callback; //(io_user_reference_t) callback;
    asyncRef[kIOAsyncCalloutRefconIndex] = pIface;

    pIface->class_async_done = 0;
    kern_return_t ret = IOConnectCallAsyncScalarMethod(pIface->port, 8, pIface->mnotification_port, asyncRef, kOSAsyncRef64Count, args, 3, NULL, NULL);

    if (ret) { 
        printf("%x\n", ret);
        return ret;
    }

#if 0
    uint64_t timeoutMs = 10000;

    uint64_t i = 0;
    for (i = 0; i < timeoutMs; i++) 
    {
        if (pIface->class_async_done) break;
        msleep(1);
    }
    if (i == timeoutMs && !pIface->class_async_done) return 0;

    pIface->class_async_done = 0;
#endif
    

    //IOUSBDeviceInterface_StallPipe(pImplCtx, iface_num, pipe_id);

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

void ep_callback(void* refcon, IOReturn result, uint64_t* arguments)
{
    libusbd_macos_ep_t* pEp = (libusbd_macos_ep_t*)refcon;
    pEp->ep_async_done = 1;
    pEp->last_read = (uint64_t)arguments;

    // TODO: why does this function also return for WritePipe on another EP?
    if (pEp->last_read)
        printf("Read %x bytes %x\n", pEp->last_read, result);
}


kern_return_t IOUSBDeviceInterface_StallPipe(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint64_t pipe_id);
kern_return_t IOUSBDeviceInterface_ReadPipe(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint64_t pipe_id, void* data, uint32_t len, uint64_t timeoutMs)
{
    // TODO bounds
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];
    libusbd_macos_ep_t* pEp = &pIface->aEndpoints[pipe_id];

    libusbd_macos_buffer_t* pBuffer = &pEp->buffer;
    uint64_t args[4] = {pipe_id, pBuffer->token, len, timeoutMs};
    uint64_t outputCount = 1;
    uint64_t output[1];

    uint64_t asyncRef[8];
    asyncRef[kIOAsyncReservedIndex] = pEp->mnotification_port;
    asyncRef[kIOAsyncCalloutFuncIndex] = ep_callback; //(io_user_reference_t) callback;
    asyncRef[kIOAsyncCalloutRefconIndex] = pEp;

    pEp->ep_async_done = 0;

    //kern_return_t ret = IOConnectCallScalarMethod(pIface->port, 13, args, 4, output, &outputCount);
    kern_return_t ret = IOConnectCallAsyncScalarMethod(pIface->port, 13, pEp->mnotification_port, asyncRef, kOSAsyncRef64Count, args, 4, output, &outputCount);

    if (ret) { 
        return ret;
    }

    uint64_t i = 0;
    for (i = 0; i < timeoutMs; i++) 
    {
        if (pEp->ep_async_done) break;
        msleep(1);
    }
    if (i == timeoutMs && !pEp->ep_async_done) {
        return 0;
    }

    pEp->ep_async_done = 0;
    ret = pEp->last_read;
    pEp->last_read = 0;

    //printf("%x %x %x\n", ret, output[0], *(uint32_t*)pBuffer->data);
    if (data && pBuffer->data && len)
        memcpy(data, pBuffer->data, len); // TODO len check

    //IOUSBDeviceInterface_StallPipe(pImplCtx, iface_num, pipe_id);

    return ret;
}

kern_return_t IOUSBDeviceInterface_ReadPipeStart(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint64_t pipe_id, uint32_t len)
{
    // TODO bounds
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];
    libusbd_macos_ep_t* pEp = &pIface->aEndpoints[pipe_id];

    libusbd_macos_buffer_t* pBuffer = &pEp->buffer;
    uint64_t args[4] = {pipe_id, pBuffer->token, len, 10};
    uint64_t outputCount = 1;
    uint64_t output[1];

    uint64_t asyncRef[8];
    asyncRef[kIOAsyncReservedIndex] = pEp->mnotification_port;
    asyncRef[kIOAsyncCalloutFuncIndex] = ep_callback; //(io_user_reference_t) callback;
    asyncRef[kIOAsyncCalloutRefconIndex] = pEp;

    kern_return_t ret = IOConnectCallAsyncScalarMethod(pIface->port, 13, pEp->mnotification_port, asyncRef, kOSAsyncRef64Count, args, 4, output, &outputCount);

    return ret;
}

kern_return_t IOUSBDeviceInterface_WritePipe(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint64_t pipe_id, void* data, uint32_t len, uint64_t timeoutMs)
{
    // TODO bounds
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];
    libusbd_macos_ep_t* pEp = &pIface->aEndpoints[pipe_id];

    libusbd_macos_buffer_t* pBuffer = &pEp->buffer;
    uint64_t args[4] = {pipe_id, pBuffer->token, len, timeoutMs};
    uint64_t outputCount = 1;
    uint64_t output[1];

    if (data && pBuffer->data && len)
        memcpy(pBuffer->data, data, len); // TODO len check

    kern_return_t ret = IOConnectCallScalarMethod(pIface->port, 14, args, 4, output, &outputCount);
    if (!ret) {
        ret = output[0];
    }

    return ret;
}

kern_return_t IOUSBDeviceInterface_StallPipe(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint64_t pipe_id)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    uint64_t args[1] = {pipe_id};

    kern_return_t ret = IOConnectCallScalarMethod(pIface->port, 15, args, 1, NULL, NULL);

    return ret;
}

kern_return_t IOUSBDeviceInterface_AbortPipe(libusbd_macos_ctx_t* pImplCtx, uint8_t iface_num, uint64_t pipe_id)
{
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];

    uint64_t args[1] = {pipe_id};

    kern_return_t ret = IOConnectCallScalarMethod(pIface->port, 16, args, 1, NULL, NULL);

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

    LaunchRunLoopThread();

    
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

    alt_IOUSBDeviceDescriptionRemoveAllConfigurations(pImplCtx->desc);

    pImplCtx->configId = alt_IOUSBDeviceDescriptionAppendConfiguration(pImplCtx->desc, CFSTR("libusbd"), 0xA0, 511);
    
    // Setup pkt source
    CFRunLoopSourceRef run_loop_source;

    // Create port to listen for kernel notifications on.
    pImplCtx->notification_port = IONotificationPortCreate(kIOMasterPortDefault);
    if (!pImplCtx->notification_port) {
        printf("Error getting notification port.\n");
        return 0;
    }

    // Get lower level mach port from notification port.
    pImplCtx->mnotification_port = IONotificationPortGetMachPort(pImplCtx->notification_port);
    if (!pImplCtx->mnotification_port) {
        printf("Error getting mach notification port.\n");
        return 0;
    }

    // Create a run loop source from our notification port so we can add the port to our run loop.
    run_loop_source = IONotificationPortGetRunLoopSource(pImplCtx->notification_port);
    if (run_loop_source == NULL) {
        printf("Error getting run loop source.\n");
        return 0;
    }

    // Add the notification port and timer to the run loop.
    CFRunLoopAddSource(_runLoop, run_loop_source, kCFRunLoopDefaultMode);

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_free(libusbd_ctx_t* pCtx)
{
    kern_return_t s_ret;

    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    StopRunLoopThread();

    // setDesc
    /*outputCount = 0;
    output[0] = some_str;
    args[0] = some_str; // idk
    s_ret = IOConnectCallMethod(pIface->port, 2, NULL, 0, args, 1, NULL, NULL, NULL, NULL);
    printf("setdesc s_ret %x %x (%x %x)\n", s_ret, outputCount, output[0], output[1]);*/

    for (int j = 0; j < pCtx->bNumInterfaces; j++)
    {
        libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[j];

        for (int i = 0; i < pIface->bNumEndpoints; i++)
        {
            libusbd_macos_ep_t* pEp = &pIface->aEndpoints[i];
            s_ret = IOUSBDeviceInterface_ReleaseBuffer(pImplCtx, j, &pEp->buffer);
            printf("release s_ret %x\n", s_ret);

            IONotificationPortDestroy(pEp->notification_port);
        }
        IONotificationPortDestroy(pIface->notification_port);

        s_ret = IOUSBDeviceInterface_Close(pImplCtx, j);
        printf("close s_ret %x\n", s_ret);

        IOServiceClose(pIface->port);

        mach_port_destroy(mach_task_self(), pIface->async_port);

        free(pIface->pName);
        pIface->pName = NULL;
    }

    IONotificationPortDestroy(pImplCtx->notification_port);

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

    if (pCtx->vid)
        alt_IOUSBDeviceDescriptionSetVendorID(pImplCtx->desc, pCtx->vid);
    if (pCtx->pid)
        alt_IOUSBDeviceDescriptionSetProductID(pImplCtx->desc, pCtx->pid);
    if (pCtx->did)
        alt_IOUSBDeviceDescriptionSetVersion(pImplCtx->desc, pCtx->did);
    alt_IOUSBDeviceDescriptionSetClass(pImplCtx->desc, pCtx->bClass);
    alt_IOUSBDeviceDescriptionSetSubClass(pImplCtx->desc, pCtx->bClass);
    alt_IOUSBDeviceDescriptionSetProtocol(pImplCtx->desc, pCtx->bClass);

    if (pCtx->pManufacturerStr) {
        CFStringRef str = CFStringCreateWithCString(NULL, pCtx->pManufacturerStr, 0);
        alt_IOUSBDeviceDescriptionSetManufacturerString(pImplCtx->desc, str);
    }
    if (pCtx->pProductStr) {
        CFStringRef str = CFStringCreateWithCString(NULL, pCtx->pProductStr, 0);
        alt_IOUSBDeviceDescriptionSetProductString(pImplCtx->desc, str);
    }
    if (pCtx->pSerialStr) {
        CFStringRef str = CFStringCreateWithCString(NULL, pCtx->pSerialStr, 0);
        alt_IOUSBDeviceDescriptionSetSerialString(pImplCtx->desc, str);
    }

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
    }

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_iface_finalize(libusbd_ctx_t* pCtx, uint8_t iface_num)
{
    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num]; // TODO bounds

    IOUSBDeviceInterface_CreateBuffer(pImplCtx, iface_num, 0x4000, &pIface->setup_buffer); // TODO EP max size, error

    for (int k = 0; k < pIface->bNumEndpoints; k++)
    {
        libusbd_macos_ep_t* pEp = &pIface->aEndpoints[k];
        IOUSBDeviceInterface_CreateBuffer(pImplCtx, iface_num, 0x4000, &pEp->buffer); // TODO EP max size, error

        CFRunLoopSourceRef run_loop_source;

        // Create port to listen for kernel notifications on.
        pEp->notification_port = IONotificationPortCreate(kIOMasterPortDefault);
        if (!pEp->notification_port) {
            printf("Error getting notification port.\n");
            return 0;
        }

        // Get lower level mach port from notification port.
        pEp->mnotification_port = IONotificationPortGetMachPort(pEp->notification_port);
        if (!pEp->mnotification_port) {
            printf("Error getting mach notification port.\n");
            return 0;
        }

        // Create a run loop source from our notification port so we can add the port to our run loop.
        run_loop_source = IONotificationPortGetRunLoopSource(pEp->notification_port);
        if (run_loop_source == NULL) {
            printf("Error getting run loop source.\n");
            return 0;
        }

        // Add the notification port and timer to the run loop.
        CFRunLoopAddSource(_runLoop, run_loop_source, kCFRunLoopDefaultMode);
    }

    IOUSBDeviceInterface_CommitConfiguration(pImplCtx, iface_num);

    IOUSBDeviceInterface_SetClassCommandCallbacks(pImplCtx, iface_num, true, false, false); // second bool sets whether to report successful sends?

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

    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num];
    pIface->bNumEndpoints++;

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

    CFRunLoopSourceRef run_loop_source;

    // Create port to listen for kernel notifications on.
    pIface->notification_port = IONotificationPortCreate(kIOMasterPortDefault);
    if (!pIface->notification_port) {
        printf("Error getting notification port.\n");
        return 0;
    }

    // Get lower level mach port from notification port.
    pIface->mnotification_port = IONotificationPortGetMachPort(pIface->notification_port);
    if (!pIface->mnotification_port) {
        printf("Error getting mach notification port.\n");
        return 0;
    }

    // Create a run loop source from our notification port so we can add the port to our run loop.
    run_loop_source = IONotificationPortGetRunLoopSource(pIface->notification_port);
    if (run_loop_source == NULL) {
        printf("Error getting run loop source.\n");
        return 0;
    }

    // Add the notification port and timer to the run loop.
    CFRunLoopAddSource(_runLoop, run_loop_source, kCFRunLoopDefaultMode);

    //pIface->async_port = create_port();

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

int libusbd_macos_iface_set_class_cmd_callback(libusbd_ctx_t* pCtx, uint8_t iface_num, libusbd_setup_callback_t func)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num]; // TODO oob

    pIface->setup_callback = func;

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_ep_read(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep, void* data, uint32_t len, uint64_t timeoutMs)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    kern_return_t ret = IOUSBDeviceInterface_ReadPipe(pImplCtx, iface_num, ep, data, len, timeoutMs);
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
        printf("Unknown error from IOUSBDeviceInterface_ReadPipe: %08x\n", ret);
        return LIBUSBD_NONDESCRIPT_ERROR;
    }

    return ret;
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

int libusbd_macos_ep_stall(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_StallPipe(pImplCtx, iface_num, ep);

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_ep_abort(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    IOUSBDeviceInterface_AbortPipe(pImplCtx, iface_num, ep);

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_ep_get_buffer(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep, void** pOut)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;
    libusbd_macos_iface_t* pIface = &pImplCtx->aInterfaces[iface_num]; // TODO oob
    libusbd_macos_ep_t* pEp = &pIface->aEndpoints[ep]; // TODO oob

    *pOut = pEp->buffer.data;

    return LIBUSBD_SUCCESS;
}

int libusbd_macos_ep_read_start(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep, uint32_t len)
{
    if (!pCtx) {
        return LIBUSBD_INVALID_ARGUMENT;
    }

    libusbd_macos_ctx_t* pImplCtx = pCtx->pMacosCtx;

    kern_return_t ret = IOUSBDeviceInterface_ReadPipeStart(pImplCtx, iface_num, ep, len);
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
        printf("Unknown error from IOUSBDeviceInterface_ReadPipe: %08x\n", ret);
        return LIBUSBD_NONDESCRIPT_ERROR;
    }

    return ret;
}