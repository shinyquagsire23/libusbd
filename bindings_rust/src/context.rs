use super::*;

use crate::error::Result;
use std::ffi::CString;
use std::ffi::c_void;
use core::pin::Pin;
use core::future::Future;
use core::task::{Poll, Waker};
use std::time::Duration;
use std::{thread, time};
use async_std::task;

// Types/bits
#[non_exhaustive]
pub struct EpDir;

impl EpDir {
    pub const IN: u8 = USB_EP_DIR_IN as u8;
    pub const OUT: u8 = USB_EP_DIR_OUT as u8;
}

#[non_exhaustive]
pub struct EpType;

impl EpType {
    pub const CTRL: u8 = USB_EPATTR_TTYPE_CTRL as u8;
    pub const ISOC: u8 = USB_EPATTR_TTYPE_ISOC as u8;
    pub const BULK: u8 = USB_EPATTR_TTYPE_BULK as u8;
    pub const INTR: u8 = USB_EPATTR_TTYPE_INTR as u8;
}

// Implementation
pub struct Context {
    context: *mut libusbd_ctx_t,
}

impl Drop for Context {
    /// Closes the `libusbd` context.
    fn drop(&mut self) {
        unsafe {
            libusbd_free(self.context);
        }
    }
}

pub struct EpFuture<'a>
{
    context: &'a Context,
    iface_num: u8, 
    ep: u64,
}

// TODO: is there a way to only wake after 1ms?rust 
impl Future for EpFuture<'_> 
{
    type Output = ();
    fn poll(self: Pin<&mut Self>, ctx: &mut core::task::Context<'_>) -> Poll<Self::Output> 
    {
        let result = self.context.ep_transfer_done(self.iface_num, self.ep);
        match result
        {
            Ok(true) => Poll::Ready(()),
            //Ok(false) => {ctx.waker().wake_by_ref(); Poll::Pending},
            _ => {
                let waker = ctx.waker().clone(); 

                // TODO: should this be more granular?
                thread::spawn(move || {
                    thread::sleep(Duration::from_millis(1));
                    waker.wake();
                });

                Poll::Pending
            },
        }
    }
}

impl Context {
    /// Opens a new `libusbd` context.
    pub fn new() -> Result<Self> {

        let mut context = core::mem::MaybeUninit::<*mut libusbd_ctx_t>::uninit();
        let context_state = unsafe {
            try_unsafe!(libusbd_init(context.as_mut_ptr()));
            context.assume_init()
        };

        Ok(Context { context: context_state })
    }

    pub fn set_vid(&self, val: u16) -> Result<()> {
        try_unsafe!(libusbd_set_vid(self.context, val));

        Ok(())
    }

    pub fn set_pid(&self, val: u16) -> Result<()> {
        try_unsafe!(libusbd_set_pid(self.context, val));

        Ok(())
    }

    pub fn set_version(&self, val: u16) -> Result<()> {
        try_unsafe!(libusbd_set_version(self.context, val));

        Ok(())
    }

    pub fn set_class(&self, val: u8) -> Result<()> {
        try_unsafe!(libusbd_set_class(self.context, val));

        Ok(())
    }

    pub fn set_subclass(&self, val: u8) -> Result<()> {
        try_unsafe!(libusbd_set_subclass(self.context, val));

        Ok(())
    }

    pub fn set_protocol(&self, val: u8) -> Result<()> {
        try_unsafe!(libusbd_set_protocol(self.context, val));

        Ok(())
    }

    #[allow(temporary_cstring_as_ptr)] // we explicitly copy the string to new memory
    pub fn set_manufacturer_str(&self, val: &str) -> Result<()> {
        try_unsafe!(libusbd_set_manufacturer_str(self.context, CString::new(val).unwrap().as_ptr()));

        Ok(())
    }

    #[allow(temporary_cstring_as_ptr)] // we explicitly copy the string to new memory
    pub fn set_product_str(&self, val: &str) -> Result<()> {
        try_unsafe!(libusbd_set_product_str(self.context, CString::new(val).unwrap().as_ptr()));

        Ok(())
    }

    #[allow(temporary_cstring_as_ptr)] // we explicitly copy the string to new memory
    pub fn set_serial_str(&self, val: &str) -> Result<()> {
        try_unsafe!(libusbd_set_serial_str(self.context, CString::new(val).unwrap().as_ptr()));

        Ok(())
    }

    pub fn config_finalize(&self) -> Result<()> {
        try_unsafe!(libusbd_config_finalize(self.context));

        Ok(())
    }

    pub fn iface_alloc(&self) -> Result<u8> {
        let mut out: u8 = 0xFF;

        try_unsafe!(libusbd_iface_alloc(self.context, &mut out));

        Ok(out)
    }

    pub fn iface_finalize(&self, iface_num: u8) -> Result<()> {
        try_unsafe!(libusbd_iface_finalize(self.context, iface_num));

        Ok(())
    }

    pub fn iface_standard_desc(&self, iface_num: u8, desc_type: u8, unk: u8, pDesc: &[u8]) -> Result<()> {
        try_unsafe!(libusbd_iface_standard_desc(self.context, iface_num, desc_type, unk, pDesc.as_ptr(), pDesc.len() as u64));

        Ok(())
    }

    pub fn iface_nonstandard_desc(&self, iface_num: u8, desc_type: u8, unk: u8, pDesc: &[u8]) -> Result<()> {
        try_unsafe!(libusbd_iface_nonstandard_desc(self.context, iface_num, desc_type, unk, pDesc.as_ptr(), pDesc.len() as u64));

        Ok(())
    }

    pub fn iface_add_endpoint(&self, iface_num: u8, ep_type: u8, direction: u8, max_pkt_size: u32, interval: u8, unk: u64) -> Result<u64> {
        let mut out: u64 = 0xFFFFFFFFFFFFFFFF;

        try_unsafe!(libusbd_iface_add_endpoint(self.context, iface_num, ep_type, direction, max_pkt_size, interval, unk, &mut out));

        Ok(out)
    }

    pub fn iface_set_class(&self, iface_num: u8, val: u8) -> Result<()> {
        try_unsafe!(libusbd_iface_set_class(self.context, iface_num, val));

        Ok(())
    }

    pub fn iface_set_subclass(&self, iface_num: u8, val: u8) -> Result<()> {
        try_unsafe!(libusbd_iface_set_subclass(self.context, iface_num, val));

        Ok(())
    }

    pub fn iface_set_protocol(&self, iface_num: u8, val: u8) -> Result<()> {
        try_unsafe!(libusbd_iface_set_protocol(self.context, iface_num, val));

        Ok(())
    }

    pub fn ep_read(&self, iface_num: u8, ep: u64, data_out: &mut [u8], timeout_ms: u64) -> Result<i32> {
        let read = try_unsafe!(libusbd_ep_read(self.context, iface_num, ep, data_out.as_mut_ptr() as *mut c_void, data_out.len() as u32, timeout_ms));

        Ok(read)
    }

    pub fn ep_write(&self, iface_num: u8, ep: u64, data_in: &[u8], timeout_ms: u64) -> Result<i32> {
        let written = try_unsafe!(libusbd_ep_write(self.context, iface_num, ep, data_in.as_ptr() as *const c_void, data_in.len() as u32, timeout_ms));

        Ok(written)
    }

    pub fn ep_stall(&self, iface_num: u8, ep: u64) -> Result<()> {
        try_unsafe!(libusbd_ep_stall(self.context, iface_num, ep));

        Ok(())
    }

    pub fn ep_abort(&self, iface_num: u8, ep: u64) -> Result<()> {
        try_unsafe!(libusbd_ep_abort(self.context, iface_num, ep));

        Ok(())
    }

    pub fn ep_transfer_done(&self, iface_num: u8, ep: u64) -> Result<bool> {
        let ret = try_unsafe!(libusbd_ep_transfer_done(self.context, iface_num, ep));

        Ok(ret != 0)
    }

    pub fn ep_write_async<'a>(&'a self, iface_num: u8, ep: u64, data_in: &[u8]) -> Result<EpFuture<'a>> {
        let written = try_unsafe!(libusbd_ep_write_start(self.context, iface_num, ep, data_in.as_ptr() as *const c_void, data_in.len() as u32));

        Ok(EpFuture { context: self, iface_num: iface_num, ep: ep })
    }

/*
int libusbd_ep_get_buffer(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep, void** pOut);
int libusbd_ep_read_start(libusbd_ctx_t* pCtx, uint8_t iface_num, uint64_t ep, uint32_t len);
*/
}