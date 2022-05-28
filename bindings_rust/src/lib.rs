#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

pub use context::{Context, EpDir, EpType};

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem;
    use std::ptr;

    use crate as libusbd;

/*
    #[test]
    fn test_init() {
        unsafe {
            let mut pCtx: *mut libusbd_ctx_t = ptr::null_mut();

            libusbd_init(&mut pCtx);

            //libusbd_set_vid(pCtx, 0x1234);
            libusbd_set_pid(pCtx, 0x1234);
            libusbd_set_version(pCtx, 0x1234);

            libusbd_set_class(pCtx, 0);
            libusbd_set_subclass(pCtx, 0);
            libusbd_set_protocol(pCtx, 0);

            libusbd_set_manufacturer_str(pCtx, CString::new("Manufacturer").unwrap().as_ptr());
            libusbd_set_product_str(pCtx, CString::new("Product").unwrap().as_ptr());
            libusbd_set_serial_str(pCtx, CString::new("Serial").unwrap().as_ptr());

            let mut iface_num: u8 = 0;
            libusbd_iface_alloc(pCtx, &mut iface_num);
            libusbd_config_finalize(pCtx);

            libusbd_iface_set_class(pCtx, iface_num, 3);
            libusbd_iface_set_subclass(pCtx, iface_num, 1);
            libusbd_iface_set_protocol(pCtx, iface_num, 1);

            libusbd_free(pCtx);
        }
    }
*/
    macro_rules! must_succeed {
        ($x:expr) => {
            match $x {
                Ok(n) => n,
                Err(err) => panic!("Got error: {:?}", err),
            }
        }
    }

    #[test]
    fn test_init() {
        let context = libusbd::Context::new().unwrap();

        must_succeed!(context.set_vid(0x1234));
        must_succeed!(context.set_pid(0x1234));

        must_succeed!(context.set_class(0));
        must_succeed!(context.set_subclass(0));
        must_succeed!(context.set_protocol(0));

        must_succeed!(context.set_manufacturer_str("Manufacturer"));
        must_succeed!(context.set_product_str("Product"));
        must_succeed!(context.set_serial_str("Serial"));

        let iface_num = match context.iface_alloc() {
            Ok(n) => n,
            Err(error) => panic!("Couldn't allocate interface: {:?}", error),
        };

        must_succeed!(context.config_finalize());

        must_succeed!(context.iface_set_class(iface_num, 3));
        must_succeed!(context.iface_set_subclass(iface_num, 1));
        must_succeed!(context.iface_set_protocol(iface_num, 1));

        let hid_desc: [u8; 9] = [0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x5D, 0x00];
        let hid_report_desc: [u8; 65] = [0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 
            0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
            0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 
            0x75, 0x08, 0x81, 0x01, 0x95, 0x03, 0x75, 0x01,
            0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91, 0x02, 
            0x95, 0x05, 0x75, 0x01, 0x91, 0x01, 0x95, 0x06,
            0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05, 
            0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x81, 0x00,
            0xC0];

        must_succeed!(context.iface_standard_desc(iface_num, 0x21, 0xF, &hid_desc));
        must_succeed!(context.iface_nonstandard_desc(iface_num, 0x22, 0xF, &hid_report_desc));
        let ep_out = match context.iface_add_endpoint(iface_num, EpType::INTR, EpDir::IN, 8, 10, 0) {
            Ok(n) => n,
            Err(error) => panic!("Couldn't add endpoint: {:?}", error),
        };
        must_succeed!(context.iface_finalize(iface_num));

        let test_send: [u8; 8] = [0x0, 0x0, 0x20, 0x0, 0x0, 0x0, 0x0, 0x0];
        let test_send_2: [u8; 8] = [0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0];

        context.ep_write(iface_num, ep_out, &test_send, 100);
        context.ep_write(iface_num, ep_out, &test_send_2, 100);

        // Context is torn down on scope exit
    }

    // TODO test finalization errors
}

#[macro_use]
mod error;

mod context;
