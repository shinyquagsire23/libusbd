use libusbd::{Context, EpType, EpDir, Error};
use std::time;
use futures::executor::block_on;
use async_std::task;
use std::io::{self, Write};
use std::thread;
use std::slice;

use std::sync::mpsc::{Sender, Receiver};
use std::sync::mpsc;

mod support;
use glutin::event::{Event, WindowEvent, DeviceEvent, ElementState, VirtualKeyCode, MouseButton, MouseScrollDelta};
use glutin::event_loop::{ControlFlow, EventLoop};
use glutin::window::WindowBuilder;
use glutin::ContextBuilder;

use std::time::Instant;

macro_rules! must_succeed {
    ($x:expr) => {
        match $x {
            Ok(n) => n,
            Err(err) => panic!("Got error: {:?}", err),
        }
    }
}

async fn speen_task() {
    let mut idx: usize = 0;

    loop {
        let speen = vec!['/', '-', '\\', '|'];
        //print!("    Speen {}\r", speen[idx % 4]);
        idx += 1;

        if idx > 60 {
            idx = 0;
        }

        io::stdout().flush().unwrap();
        task::sleep(time::Duration::from_millis(1)).await;
    }
}

struct SentKeypress
{
    scancode: u32,
    pressed: bool,
}

struct SentMouse
{
    button: u8,
    pressed: bool,
    dx: i16,
    dy: i16,
    wheel_dx: i8,
    wheel_dy: i8,
}

async fn usb_print_task(rx: Receiver<SentKeypress>, rx_mouse: Receiver<SentMouse>)
{
    let context = libusbd::Context::new().unwrap();

    must_succeed!(context.set_vid(0x057e)); // Nintendo
    must_succeed!(context.set_pid(0x201E)); // N64 controller
    must_succeed!(context.set_version(0x0212)); // 2.12

    must_succeed!(context.set_class(0));
    must_succeed!(context.set_subclass(0));
    must_succeed!(context.set_protocol(0));

    must_succeed!(context.set_manufacturer_str("Nintendo Co., Ltd."));
    must_succeed!(context.set_product_str("N64 Controller"));
    must_succeed!(context.set_serial_str("00000000001"));

    let iface_num = match context.iface_alloc() {
        Ok(n) => n,
        Err(error) => panic!("Couldn't allocate interface: {:?}", error),
    };

    must_succeed!(context.config_finalize());

    must_succeed!(context.iface_set_class(iface_num, 3));
    must_succeed!(context.iface_set_subclass(iface_num, 0));
    must_succeed!(context.iface_set_protocol(iface_num, 0));


    let hid_desc: [u8; 9] = [0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 203, 0x00];
    let hid_report_desc: [u8; 203] = [
        0x05, 0x01, 0x15, 0x00, 0x09, 0x04, 0xA1, 0x01, 
        0x85, 0x30, 0x05, 0x01, 0x05, 0x09, 0x19, 0x01, 
        0x29, 0x0A, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 
        0x95, 0x0A, 0x55, 0x00, 0x65, 0x00, 0x81, 0x02, 
        0x05, 0x09, 0x19, 0x0B, 0x29, 0x0E, 0x15, 0x00, 
        0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02, 
        0x75, 0x01, 0x95, 0x02, 0x81, 0x03, 0x0B, 0x01, 
        0x00, 0x01, 0x00, 0xA1, 0x00, 0x0B, 0x30, 0x00, 
        0x01, 0x00, 0x0B, 0x31, 0x00, 0x01, 0x00, 0x0B, 
        0x32, 0x00, 0x01, 0x00, 0x0B, 0x35, 0x00, 0x01, 
        0x00, 0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 
        0x75, 0x10, 0x95, 0x04, 0x81, 0x02, 0xC0, 0x0B, 
        0x39, 0x00, 0x01, 0x00, 0x15, 0x00, 0x25, 0x07, 
        0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 
        0x04, 0x95, 0x01, 0x81, 0x02, 0x05, 0x09, 0x19, 
        0x0F, 0x29, 0x12, 0x15, 0x00, 0x25, 0x01, 0x75, 
        0x01, 0x95, 0x04, 0x81, 0x02, 0x75, 0x08, 0x95, 
        0x34, 0x81, 0x03, 0x06, 0x00, 0xFF, 0x85, 0x21, 
        0x09, 0x01, 0x75, 0x08, 0x95, 0x3F, 0x81, 0x03, 
        0x85, 0x81, 0x09, 0x02, 0x75, 0x08, 0x95, 0x3F, 
        0x81, 0x03, 0x85, 0x01, 0x09, 0x03, 0x75, 0x08, 
        0x95, 0x3F, 0x91, 0x83, 0x85, 0x10, 0x09, 0x04, 
        0x75, 0x08, 0x95, 0x3F, 0x91, 0x83, 0x85, 0x80, 
        0x09, 0x05, 0x75, 0x08, 0x95, 0x3F, 0x91, 0x83, 
        0x85, 0x82, 0x09, 0x06, 0x75, 0x08, 0x95, 0x3F, 
        0x91, 0x83, 0xC0
    ];

    must_succeed!(context.iface_standard_desc(iface_num, 0x21, 0xF, &hid_desc));
    must_succeed!(context.iface_nonstandard_desc(iface_num, 0x22, 0xF, &hid_report_desc));
    let ep_in = match context.iface_add_endpoint(iface_num, EpType::INTR, EpDir::OUT, 64, 1, 0) {
        Ok(n) => n,
        Err(error) => panic!("Couldn't add endpoint: {:?}", error),
    };
    let ep_out = match context.iface_add_endpoint(iface_num, EpType::INTR, EpDir::IN, 64, 1, 0) {
        Ok(n) => n,
        Err(error) => panic!("Couldn't add endpoint: {:?}", error),
    };
    must_succeed!(context.iface_finalize(iface_num));

    // 1 joy-con left
    // 2 joy-con right
    // 3 pro controller
    // 4 crash
    // 5 crash
    // 6 generic pro controller (no colors)
    // 7 Famicom controller left
    // 8 Famicom controller right
    // 9 NES controller
    // 0xA NES controller
    // 0xB SNES controller (EUR)
    // 0xC n64
    // 0xD generic usb
    // 0xE generic usb
    // 0xF rejected?
    // 0x10-13 rejected?
    let device_type: u8 = 0xc;

    let mut should_stream_input = false;
    let mut keyboard_send: [u8; 8] = [0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0];
    let mut mouse_send: [u8; 0x8] = [0x1, 0x0,  0x0, 0x0,  0x0, 0x0,   0x0, 0x0];
    let mut mouse_btns: u8 = 0;

    let mut controller_send: [u8; 64] = [0x0; 64];
    let mut controller_recv: [u8; 64] = [0x0; 64];

    loop {
        controller_send[0] = 0x81;
        controller_send[1] = 0x01;


        controller_send[2] = 0x00;
        controller_send[3] = device_type;

        controller_send[4] = 0x34;
        controller_send[5] = 0x12;
        controller_send[6] = 0xe5;
        controller_send[7] = 0xbf;
        controller_send[8] = 0x03;
        controller_send[9] = 0x7d; // mac
                
        let send_future_1 = match context.ep_write_async(iface_num, ep_out, &controller_send, 1000)
        {
            Ok(f) => f,
            Err(err) => {
                println!("Got error: {:?}", err);
                task::sleep(time::Duration::from_millis(16)).await;
                continue;
            },
        };

        // This also reports the number of bytes written, if Ok
        match send_future_1.await {
            Err(err) => {
                println!("Got error: {:?}", err);
                task::sleep(time::Duration::from_millis(16)).await;
                continue;
            },
            Ok(_) => (),
        };
        break;
    }

    println!("begin loop");

    let mut last_loop = Instant::now();
    let mut last_input = Instant::now();

    let mut controller_needs_update = false;
    let mut pkt_increment = 0;
    loop {
        
        let mut needs_update = false;
        let mut mouse_needs_update = false;

        for _i in 0..6 {
            let event = match rx.try_recv() {
                Ok(event) => event,
                _ => { break; },
            };

            let trunc_scancode: u8 = (event.scancode & 0xFF) as u8;

            if event.pressed {
                let mut already_pressed = false;
                for j in 0..6 {
                    if keyboard_send[2+j] == trunc_scancode {
                        already_pressed = true;
                    }
                }

                if !already_pressed {
                    for j in 0..6 {
                        if keyboard_send[2+j] == 0 {
                            keyboard_send[2+j] = trunc_scancode;
                            needs_update = true;
                            break;
                        }
                    }
                }
            }
            else {
                for j in 0..6 {
                    if keyboard_send[2+j] == trunc_scancode {
                        keyboard_send[2+j] = 0;
                        needs_update = true;
                    }
                }
            }

            //println!("{} {}", event.scancode, event.pressed);
        }

        let mut dx = 0;
        let mut dy = 0;
        let mut wheel_dx: i16 = 0;
        let mut wheel_dy: i16 = 0;

        for _i in 0..100 {
            let event = match rx_mouse.try_recv() {
                Ok(event) => event,
                _ => { break; },
            };

            let btn: u8 = event.button;

            if event.pressed {
                mouse_btns |= btn;
            }
            else {
                mouse_btns &= !btn;
            }

            dx += event.dx;
            dy += event.dy;

            wheel_dx += event.wheel_dx as i16;
            wheel_dy += event.wheel_dy as i16;

            wheel_dx = wheel_dx.max(-127).min(127);
            wheel_dy = wheel_dy.max(-127).min(127);

            mouse_needs_update = true;

            //println!("{} {}", event.scancode, event.pressed);
        }

        mouse_send[1] = mouse_btns;
        mouse_send[2] = (dx as u16 & 0xFF) as u8;
        mouse_send[3] = ((dx as u16 & 0xFF00) >> 8) as u8;
        mouse_send[4] = (dy as u16 & 0xFF) as u8;
        mouse_send[5] = ((dy as u16 & 0xFF00) >> 8) as u8;
        mouse_send[6] = wheel_dy as u8;
        mouse_send[7] = wheel_dx as u8;
        if wheel_dy != 0 {
            println!("{}", wheel_dy);
        }

        if needs_update || mouse_needs_update {
            //println!("{:?} {:?}", keyboard_send, mouse_send);
        }


        // map keyboard to buttons
        let mut controller_buttons: u32 = 0x008000; // charging grip?
        let mut stick_1_x: u16 = 0x7C9;
        let mut stick_1_y: u16 = 0x842;
        let mut stick_2_x: u16 = 0x7C9;
        let mut stick_2_y: u16 = 0x842;

        for j in 0..6 {
            let k = keyboard_send[2+j];
            if k == 0 {
                continue;
            }
            /*
            VirtualKeyCode::A => 0x4,
        VirtualKeyCode::B => 0x5,
        VirtualKeyCode::C => 0x6,
        VirtualKeyCode::D => 0x7,
        VirtualKeyCode::E => 0x8,
        VirtualKeyCode::F => 0x9,
        VirtualKeyCode::G => 0xa,
        VirtualKeyCode::H => 0xb,
        VirtualKeyCode::I => 0xc,
        VirtualKeyCode::J => 0xd,
        VirtualKeyCode::K => 0xe,
        VirtualKeyCode::L => 0xf,
        VirtualKeyCode::M => 0x10,
        VirtualKeyCode::N => 0x11,
        VirtualKeyCode::O => 0x12,
        VirtualKeyCode::P => 0x13,
        VirtualKeyCode::Q => 0x14,
        VirtualKeyCode::R => 0x15,
        VirtualKeyCode::S => 0x16,
        VirtualKeyCode::T => 0x17,
        VirtualKeyCode::U => 0x18,
        VirtualKeyCode::V => 0x19,
        VirtualKeyCode::W => 0x1a,
        VirtualKeyCode::X => 0x1b,
        VirtualKeyCode::Y => 0x1c,
        VirtualKeyCode::Z => 0x1d,
            */

            controller_buttons |= match k {
                0x50 => 0x000008, // left
                0x4F => 0x000004, // right
                0x52 => 0x000002, // up
                0x51 => 0x000001, // down
                0x1B => 0x080000, // kb x -> a
                0x1D => 0x040000, // kb z -> b

                0x1A => 0x010000, // kb w -> n64 c-up/y
                0x04 => 0x020000, // kb a -> n64 c-left/x
                0x16 => 0x800000, // kb s -> n64 c-down/zr
                0x07 => 0x000100, // kb d -> n64 c-right/minus

                0x1e => 0x100000, // kb 1 -> SR
                0x1f => 0x200000, // kb 2 -> SL
                0x20 => 0x000800, // kb 3 -> N64 ZL/Left stick click
                0x21 => 0x000400, // kb 4 -> Right stick click

                0x14 => 0x000040, // kb q -> L
                0x08 => 0x400000, // kb e -> R

                0x28 => 0x000200, // kb return -> n64 start/plus
                0x31 => 0x001000, // kb backslash -> home?
                0x27 => 0x002000, // kb 0 -> screenshot

                0x15 => 0x000080, // kb r -> ZL
                0x09 => 0x800000, // kb f -> ZR
                _ => 0,
            };
            
        }    


        let elapsed = last_loop.elapsed();
        last_loop = Instant::now();
        if elapsed.as_millis() != 0 {
            println!("{:?}ms", elapsed.as_millis());
        }

        let mut recv_future_1 = None;
        recv_future_1 = match context.ep_read_async(iface_num, ep_in, 64, 100)
        {
            Ok(f) => Some(f),
            Err(err) => {
                println!("Got error: {:?}", err);
                //task::sleep(time::Duration::from_millis(1000)).await;
                //continue;
                None
            },
        };

        let mut send_future_1 = None;
        if controller_needs_update {
            send_future_1 = match context.ep_write_async(iface_num, ep_out, &controller_send, 100)
            {
                Ok(f) => Some(f),
                Err(err) => {
                    println!("Got error ep_write_async: {:?}", err);
                    //task::sleep(time::Duration::from_millis(1000)).await;
                    //continue;
                    None
                },
            };
            controller_needs_update = false;
        }

        // This also reports the number of bytes written, if Ok
        if let Some(f) = send_future_1 {
            match f.await {
                Err(err) => {
                    println!("Got error send_future_1: {:?}", err);
                    //task::sleep(time::Duration::from_millis(1000)).await;
                    //continue;
                },
                Ok(_n) => {
                    //println!("Sent {:?} bytes: {:02x?}", _n, controller_send);
                },
            };
        }

        // This also reports the number of bytes received, if Ok
        if let Some(f) = recv_future_1 {
            match f.await {
                Err(err) => {
                    match err {
                        Error::Timeout => {},
                        _ => {
                            println!("Got error recv_future_1: {:?}", err);
                        }
                    }
                    //task::sleep(time::Duration::from_millis(1)).await;
                    //continue;
                },
                Ok(_n) => {
                    //println!("Recv {:?} bytes", _n);
                },
            };
        }
        else {
            task::sleep(time::Duration::from_millis(1)).await;
            continue;
        }

        let read_bytes = match context.ep_transferred_bytes(iface_num, ep_in)
        {
            Ok(n) => (n & 0xFF) as usize,
            Err(err) => {
                println!("Got error ep_transferred_bytes: {:?}", err);
                    //task::sleep(time::Duration::from_millis(1000)).await;
                    //continue;
                    0
            }
        };

        if read_bytes == 0 {

            let input_elapsed = (last_input.elapsed().as_millis() & 0xFF) as u8;

            if input_elapsed >= 8 && should_stream_input {
                controller_send[0] = 0x30;
                controller_send[1] = pkt_increment;
                controller_send[2] = 0x91; // full+charging (9), USB connection (1)
                controller_send[3] = ((controller_buttons >> 16) & 0xFF) as u8; // buttons
                controller_send[4] = ((controller_buttons >> 8) & 0xFF) as u8; // buttons
                controller_send[5] = ((controller_buttons >> 0) & 0xFF) as u8; // buttons
                controller_send[6] = (stick_1_x & 0xFF) as u8; // sticks
                controller_send[7] = (((stick_1_x >> 8) & 0xF) | (stick_1_y & 0xF)) as u8; // sticks
                controller_send[8] = ((stick_1_y >> 4) & 0xFF) as u8; // sticks
                controller_send[9] = (stick_2_x & 0xFF) as u8; // sticks 2
                controller_send[10] = (((stick_2_x >> 8) & 0xF) | (stick_2_y & 0xF)) as u8; // sticks 2
                controller_send[11] = ((stick_2_y >> 4) & 0xFF) as u8; // sticks 2
                controller_send[12] = 8; // vibration input report

                last_input = Instant::now();
                pkt_increment = pkt_increment.wrapping_add(input_elapsed);
                controller_needs_update = true;
            }

            task::yield_now().await;
            task::sleep(time::Duration::from_millis(1)).await;
            continue;
        }

        let read_data = match context.ep_get_buffer(iface_num, ep_in)
        {
            Ok(f) => Some(unsafe { slice::from_raw_parts_mut(f.as_mut_ptr() as *mut u8, read_bytes as usize) }),
            Err(err) => {
                println!("Got error ep_get_buffer: {:?}", err);
                None
            }
        };

        // Process any received data
        if let Some(data) = read_data {
            println!("Got data: {:?} len, {:02x?}", read_bytes, data);

            for i in 0..64 {
                controller_send[i] = 0;
            }

            if data[0] == 0x80 && read_bytes == 2 {
                controller_send[0] = 0x81;
                controller_send[1] = data[1];

                // Controller was asked to stop USB
                if data[1] == 0x5 {
                    break;
                }
                else if data[1] == 0x1 {
                    controller_send[2] = 0x00;
                    controller_send[3] = device_type;

                    controller_send[4] = 0x34;
                    controller_send[5] = 0x12;
                    controller_send[6] = 0xe5;
                    controller_send[7] = 0xbf;
                    controller_send[8] = 0x03;
                    controller_send[9] = 0x7d; // mac
                }

                controller_needs_update = true;
            }
            else if data[0] == 0x10 { // Rumble only
                let input_elapsed = (last_input.elapsed().as_millis() & 0xFF) as u8;

                controller_send[0] = 0x30;
                controller_send[1] = pkt_increment;
                controller_send[2] = 0x91; // full+charging (9), USB connection (1)
                controller_send[3] = ((controller_buttons >> 16) & 0xFF) as u8; // buttons
                controller_send[4] = ((controller_buttons >> 8) & 0xFF) as u8; // buttons
                controller_send[5] = ((controller_buttons >> 0) & 0xFF) as u8; // buttons
                controller_send[6] = (stick_1_x & 0xFF) as u8; // sticks
                controller_send[7] = (((stick_1_x >> 8) & 0xF) | (stick_1_y & 0xF)) as u8; // sticks
                controller_send[8] = ((stick_1_y >> 4) & 0xFF) as u8; // sticks
                controller_send[9] = (stick_2_x & 0xFF) as u8; // sticks 2
                controller_send[10] = (((stick_2_x >> 8) & 0xF) | (stick_2_y & 0xF)) as u8; // sticks 2
                controller_send[11] = ((stick_2_y >> 4) & 0xFF) as u8; // sticks 2
                controller_send[12] = 8; // vibration input report

                last_input = Instant::now();
                pkt_increment = pkt_increment.wrapping_add(input_elapsed);
                controller_needs_update = true;
            }
            else if data[0] == 0x1 { // Rumble + subcommand
                let subcmd = data[10];
                let mut subcmd_reply: [u8; 35] = [0; 35];

                let input_elapsed = (last_input.elapsed().as_millis() & 0xFF) as u8;

                controller_send[0] = 0x21;
                controller_send[1] = pkt_increment;
                controller_send[2] = 0x91; // full+charging (9), USB connection (1)
                controller_send[3] = ((controller_buttons >> 16) & 0xFF) as u8; // buttons
                controller_send[4] = ((controller_buttons >> 8) & 0xFF) as u8; // buttons
                controller_send[5] = ((controller_buttons >> 0) & 0xFF) as u8; // buttons
                controller_send[6] = (stick_1_x & 0xFF) as u8; // sticks
                controller_send[7] = (((stick_1_x >> 8) & 0xF) | (stick_1_y & 0xF)) as u8; // sticks
                controller_send[8] = ((stick_1_y >> 4) & 0xFF) as u8; // sticks
                controller_send[9] = (stick_2_x & 0xFF) as u8; // sticks 2
                controller_send[10] = (((stick_2_x >> 8) & 0xF) | (stick_2_y & 0xF)) as u8; // sticks 2
                controller_send[11] = ((stick_2_y >> 4) & 0xFF) as u8; // sticks 2
                controller_send[12] = 8; // vibration input report
                controller_send[13] = 0x80; // ACK
                controller_send[14] = subcmd;

                let arg0 = data[11];
                let arg1 = data[12];
                let arg2 = data[13];
                let arg3 = data[14];

                let arg0_u32 = arg0 as u32 | (arg1 as u32) << 8 | (arg2 as u32) << 16 | (arg3 as u32) << 24;

                match subcmd {
                    0x01 => {
                        println!("Bluetooth manual pairing sent");
                        controller_send[13] = 0x81; // ACK
                        subcmd_reply[0] = 0x3;
                    },
                    0x02 => {
                        println!("Device info requested");
                        controller_send[13] = 0x82; // ACK
                        subcmd_reply[0] = 0x04; // firmware version major
                        subcmd_reply[1] = 0x11; // firmware version minor

                        subcmd_reply[2] = device_type; // type

                        subcmd_reply[3] = 0x02; // unk

                        subcmd_reply[4] = 0x34;
                        subcmd_reply[5] = 0x12;
                        subcmd_reply[6] = 0xe5;
                        subcmd_reply[7] = 0xbf;
                        subcmd_reply[8] = 0x03;
                        subcmd_reply[9] = 0x7d; // mac

                        subcmd_reply[10] = 0x00;
                        subcmd_reply[11] = 0x03; // 01, spi colors get used. 03 unk, N64?
                    },
                    0x03 => {
                        println!("Update mode is now: {:02x}", arg0);
                        // TODO: check value
                        should_stream_input = true;
                    },
                    0x04 => {
                        controller_send[13] = 0x83; // ACK?
                    },
                    0x08 => {
                        println!("Shipment low power state is now: {:02x}", arg0);
                    },
                    0x10 => {
                        let addr = arg0_u32;
                        let len = data[15];
                        println!("SPI Flash read: {:08x} {:02x}", addr, len);

                        controller_send[13] = 0x90;

                        for i in 0..5 {
                            subcmd_reply[i] = data[11+i];
                        }

                        if addr == 0x6000 && len == 16 {
                            let dat_6000: [u8; 16] = [0x00, 0x00, 0x58, 0x53, 0x4c, 0x34, 0x30, 0x30, 0x30, 0x30, 0x39, 0x30, 0x30, 0x30, 0x30, 0x30];
                            for i in 0..16 {
                                subcmd_reply[5+i] = dat_6000[i];
                            }
                        }
                        else if addr == 0x6050 && len == 0xD {
                            let dat_6050: [u8; 0xD] = [0xac, 0xac, 0xac, 0x46, 0x46, 0x46, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01];
                            for i in 0..0xD {
                                subcmd_reply[5+i] = dat_6050[i];
                            }
                        }
                        else if addr == 0x6080 && len == 0x18 {
                            let dat: [u8; 0x18] = [0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x70, 0x5d, 0x50, 0x30, 0xf3, 0x38, 0x84, 0x43, 0x38, 0x84, 0x43, 0x33, 0x39, 0x93, 0xcd, 0xd6, 0x6c];
                            for i in 0..0x18 {
                                subcmd_reply[5+i] = dat[i];
                            }
                        }
                        else if addr == 0x6098 && len == 0x12 {
                            let dat: [u8; 0x12] = [0xff; 0x12];
                            for i in 0..0x12 {
                                subcmd_reply[5+i] = dat[i];
                            }
                        }
                        else if addr == 0x8010 && len == 0x18 {
                            let dat: [u8; 0x18] = [0xff; 0x18];
                            for i in 0..0x18 {
                                subcmd_reply[5+i] = dat[i];
                            }
                        }
                        else if addr == 0x603D && len == 0x19 {
                            //let dat: [u8; 0x19] = [0x7c, 0xd5, 0x59, 0xca, 0x37, 0x84, 0x45, 0x95, 0x57, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xac, 0xac, 0xac, 0x46, 0x46, 0x46];
                            let dat: [u8; 0x19] = [0xfe, 0xc5, 0x5e, 0xec, 0x97, 0x77, 0xca, 0x95, 0x5c, 0xff, 0x57, 0x78, 0x0c, 0xc6, 0x5e, 0xb7, 0x95, 0x65, 0xff, 0x32, 0x32, 0x32, 0xff, 0xff, 0xff];
                            for i in 0..0x19 {
                                subcmd_reply[5+i] = dat[i];
                            }
                        }
                        else if addr == 0x6020 && len == 0x18 {
                            let dat: [u8; 0x18] = [0x73, 0x00, 0x9d, 0xff, 0x8c, 0x01, 0x00, 0x40, 0x00, 0x40, 0x00, 0x40, 0xde, 0xff, 0x0d, 0x00, 0x08, 0x00, 0xe7, 0x3b, 0xe7, 0x3b, 0xe7, 0x3b];//[0xff;0x18];
                            for i in 0..0x18 {
                                subcmd_reply[5+i] = dat[i];
                            }
                        }
                        else {
                            println!("Unable to provide SPI data!");
                        }
                    },
                    0x30 => {
                        println!("Lights are now: {:02x}", arg0);
                    },
                    0x38 => {
                        println!("Attempted to set HOME light");
                    },
                    0x40 => {
                        println!("Set IMU: {:02x}", arg0);
                    },
                    0x48 => {
                        println!("Vibration is now: {:02x}", arg0);
                    }
                    _ => {
                        println!("Unknown subcommand: {:02x}", subcmd);
                    }
                }
                

                for i in 0..35 {
                    controller_send[15+i] = subcmd_reply[i];
                }

                last_input = Instant::now();
                pkt_increment = pkt_increment.wrapping_add(input_elapsed);
                controller_needs_update = true;
            }
            else {
                println!("Unknown command: {:02x}", data[0]);
            }
        }

        task::yield_now().await;

        task::sleep(time::Duration::from_millis(1)).await;
        
        //task::sleep(time::Duration::from_millis(1)).await;
    }
}

fn real_scancode(keycode: VirtualKeyCode) -> u32 {
    match keycode {
        VirtualKeyCode::Key1 => 0x1e,
        VirtualKeyCode::Key2 => 0x1f,
        VirtualKeyCode::Key3 => 0x20,
        VirtualKeyCode::Key4 => 0x21,
        VirtualKeyCode::Key5 => 0x22,
        VirtualKeyCode::Key6 => 0x23,
        VirtualKeyCode::Key7 => 0x24,
        VirtualKeyCode::Key8 => 0x25,
        VirtualKeyCode::Key9 => 0x26,
        VirtualKeyCode::Key0 => 0x27,

        VirtualKeyCode::A => 0x4,
        VirtualKeyCode::B => 0x5,
        VirtualKeyCode::C => 0x6,
        VirtualKeyCode::D => 0x7,
        VirtualKeyCode::E => 0x8,
        VirtualKeyCode::F => 0x9,
        VirtualKeyCode::G => 0xa,
        VirtualKeyCode::H => 0xb,
        VirtualKeyCode::I => 0xc,
        VirtualKeyCode::J => 0xd,
        VirtualKeyCode::K => 0xe,
        VirtualKeyCode::L => 0xf,
        VirtualKeyCode::M => 0x10,
        VirtualKeyCode::N => 0x11,
        VirtualKeyCode::O => 0x12,
        VirtualKeyCode::P => 0x13,
        VirtualKeyCode::Q => 0x14,
        VirtualKeyCode::R => 0x15,
        VirtualKeyCode::S => 0x16,
        VirtualKeyCode::T => 0x17,
        VirtualKeyCode::U => 0x18,
        VirtualKeyCode::V => 0x19,
        VirtualKeyCode::W => 0x1a,
        VirtualKeyCode::X => 0x1b,
        VirtualKeyCode::Y => 0x1c,
        VirtualKeyCode::Z => 0x1d,

        VirtualKeyCode::Escape => 0x29,

        VirtualKeyCode::F1 => 0x3a,
        VirtualKeyCode::F2 => 0x3b,
        VirtualKeyCode::F3 => 0x3c,
        VirtualKeyCode::F4 => 0x3d,
        VirtualKeyCode::F5 => 0x3e,
        VirtualKeyCode::F6 => 0x3f,
        VirtualKeyCode::F7 => 0x40,
        VirtualKeyCode::F8 => 0x41,
        VirtualKeyCode::F9 => 0x42,
        VirtualKeyCode::F10 => 0x43,
        VirtualKeyCode::F11 => 0x44,
        VirtualKeyCode::F12 => 0x45,
        VirtualKeyCode::F13 => 0x68,
        VirtualKeyCode::F14 => 0x69,
        VirtualKeyCode::F15 => 0x6a,
        VirtualKeyCode::F16 => 0x6b,
        VirtualKeyCode::F17 => 0x6c,
        VirtualKeyCode::F18 => 0x6d,
        VirtualKeyCode::F19 => 0x6e,
        VirtualKeyCode::F20 => 0x6f,
        VirtualKeyCode::F21 => 0x70,
        VirtualKeyCode::F22 => 0x71,
        VirtualKeyCode::F23 => 0x72,
        VirtualKeyCode::F24 => 0x73,

        VirtualKeyCode::Snapshot => 0x46,
        VirtualKeyCode::Scroll => 0x47,
        VirtualKeyCode::Pause => 0x48,
        VirtualKeyCode::Insert => 0x49,
        VirtualKeyCode::Home => 0x4a,
        VirtualKeyCode::Delete => 0x4c,
        VirtualKeyCode::End => 0x4d,
        VirtualKeyCode::PageDown => 0x4e,
        VirtualKeyCode::PageUp => 0x4b,
        VirtualKeyCode::Left => 0x50,
        VirtualKeyCode::Up => 0x52,
        VirtualKeyCode::Right => 0x4f,
        VirtualKeyCode::Down => 0x51,
        VirtualKeyCode::Back => 0x2a,
        VirtualKeyCode::Return => 0x28,
        VirtualKeyCode::Space => 0x2c,
        VirtualKeyCode::Numlock => 0x53,
        VirtualKeyCode::Numpad0 => 0x62,
        VirtualKeyCode::Numpad1 => 0x59,
        VirtualKeyCode::Numpad2 => 0x5a,
        VirtualKeyCode::Numpad3 => 0x5b,
        VirtualKeyCode::Numpad4 => 0x5c,
        VirtualKeyCode::Numpad5 => 0x5d,
        VirtualKeyCode::Numpad6 => 0x5e,
        VirtualKeyCode::Numpad7 => 0x5f,
        VirtualKeyCode::Numpad8 => 0x60,
        VirtualKeyCode::Numpad9 => 0x61,
        VirtualKeyCode::NumpadAdd => 0x57,
        VirtualKeyCode::NumpadDivide => 0x54,
        VirtualKeyCode::NumpadComma => 0x85,
        VirtualKeyCode::NumpadDecimal => 0x63,
        VirtualKeyCode::NumpadEnter => 0x58,
        VirtualKeyCode::NumpadEquals => 0x67,
        VirtualKeyCode::NumpadMultiply => 0x55,
        VirtualKeyCode::NumpadSubtract => 0x56,

        VirtualKeyCode::Apostrophe => 0x34,
        VirtualKeyCode::Apps => 0x65,
        VirtualKeyCode::Asterisk => 0x55,
        // At, Ax
        VirtualKeyCode::Backslash => 0x31,
        VirtualKeyCode::Calculator => 0xfb,
        VirtualKeyCode::Capital => 0x39,
        VirtualKeyCode::Colon => 0x33, // ??
        VirtualKeyCode::Comma => 0x36,
        // Convert
        VirtualKeyCode::Equals => 0x2e,
        VirtualKeyCode::Grave => 0x35,
        // Kana, Kanji
        VirtualKeyCode::LAlt => 0xe2,
        VirtualKeyCode::LBracket => 0x2f,
        VirtualKeyCode::LControl => 0xe0,
        VirtualKeyCode::LShift => 0xe1,
        VirtualKeyCode::LWin => 0xe3,
        //Mail =>  
        // MediaSelect, MediaStop,
        VirtualKeyCode::Minus => 0x2d,
        // Mute, MyComputer, NavigateForward, NavigateBackward, NextTrack, NoConvert, OEM102,
        VirtualKeyCode::Period => 0x37,
        // PlayPause
        //Plus => 
        VirtualKeyCode::RAlt => 0xe6,
        VirtualKeyCode::RBracket => 0x30,
        VirtualKeyCode::RControl => 0xe4,
        VirtualKeyCode::RShift => 0xe5,
        VirtualKeyCode::RWin => 0xe7,
        VirtualKeyCode::Semicolon => 0x33,
        VirtualKeyCode::Slash => 0x38,
        // Sleep, Stop, Sysrq,
        VirtualKeyCode::Tab => 0x2b,
        // Underline, Unlabeled, VolDown, VolUp, Wake, WebBack..WebStop, Yen, Copy, Paste, Cut

        _ => 0x0,
    }
}

fn convert_mousebuttons(button: MouseButton) -> u8 {
    match button {
        MouseButton::Left => 0x1,
        MouseButton::Right => 0x2,
        MouseButton::Middle => 0x4,
        _ => 0x0,
    }
}

fn window(tx: Sender<SentKeypress>, tx_mouse: Sender<SentMouse>) 
{
    let el = EventLoop::new();
    let wb = WindowBuilder::new().with_title("Capture window");

    let windowed_context = ContextBuilder::new().build_windowed(wb, &el).unwrap();

    let windowed_context = unsafe { windowed_context.make_current().unwrap() };

    println!("Pixel format of the window's GL context: {:?}", windowed_context.get_pixel_format());

    let gl = support::load(&windowed_context.context());

    el.run(move |event, _, control_flow| {
        //println!("{:?}", event);
        *control_flow = ControlFlow::Wait;

        match event {
            Event::LoopDestroyed => return,
            Event::WindowEvent { event, .. } => match event {
                WindowEvent::Resized(physical_size) => windowed_context.resize(physical_size),
                WindowEvent::CloseRequested => *control_flow = ControlFlow::Exit,
                WindowEvent::KeyboardInput { input, .. } => {
                    let code = real_scancode(input.virtual_keycode.unwrap());
                    //println!("{:?} {:x}", input, code);
                    tx.send(SentKeypress { scancode: code, pressed: input.state == ElementState::Pressed});
                },
                WindowEvent::MouseInput { button, state, .. } => {
                    //println!("{:?}", event);
                    tx_mouse.send(SentMouse { button:convert_mousebuttons(button), pressed: state == ElementState::Pressed, dx: 0, dy: 0, wheel_dx: 0, wheel_dy: 0 });
                },
                _ => (),
            },
            Event::RedrawRequested(_) => {
                gl.draw_frame([1.0, 0.5, 0.7, 1.0]);
                windowed_context.swap_buffers().unwrap();
            },
            Event::DeviceEvent { event, .. } => match event {
                DeviceEvent::MouseMotion { delta } => {
                    //println!("{:?}", event);
                    tx_mouse.send(SentMouse { button:0, pressed: false, dx: delta.0 as i16, dy: delta.1 as i16, wheel_dx: 0, wheel_dy: 0 });
                },
                DeviceEvent::MouseWheel { delta } => {
                    //println!("{:?}", event);
                    let mut dx = 0;
                    let mut dy = 0;

                    match delta {
                        MouseScrollDelta::LineDelta(a,b) => {
                            let mut f_dx = a.max(-1.0).min(1.0);
                            let mut f_dy = b.max(-1.0).min(1.0);
                            dx = (f_dx * 127.0) as i8;
                            dy = (f_dy * 127.0) as i8;
                        },
                        _ => (),
                    };
                    tx_mouse.send(SentMouse { button:0, pressed: false, dx: 0, dy: 0, wheel_dx: dx, wheel_dy: dy });
                },
                _ => (),
            },
            _ => (),
        }
    });
}

fn main() {
    let (tx, rx): (Sender<SentKeypress>, Receiver<SentKeypress>) = mpsc::channel();
    let (tx_mouse, rx_mouse): (Sender<SentMouse>, Receiver<SentMouse>) = mpsc::channel();

    let handle = thread::spawn(move || { 
        block_on(async { futures::join!(usb_print_task(rx, rx_mouse), speen_task()) } );
    });
    window(tx, tx_mouse);
    handle.join().unwrap();
    
}
