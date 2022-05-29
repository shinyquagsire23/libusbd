# libusbd

Cross-OS library for implementing USB device-mode interfaces. WIP. I'm currently working out most of the API details with the programs in `examples/`.

# Current Support
 - macOS IOUSBDeviceFamily
   - Requires `usbgadget.kext` from https://github.com/shinyquagsire23/macos_usb_gadget_poc
 - Rust bindings (TODO: split into another repo?)

# Planned Support
 - Linux FunctionFS

# Provided Examples:
 - `examples/keyboard`: Emulates a HID keyboard that types `My laptop is a keyboard. ` forever.
 - `examples/gamepad`: Emulates a (Switch-compatible) HORI gamepad.
 - `examples/ums`: Emulates a USB Mass Storage device from an image file, including writing. [Stress tested as a flashdrive-less Ubuntu LiveUSB](https://www.youtube.com/watch?v=MR_B6qVGMl0).
 - `examples/rust_async`: Emulates a HID keyboard using Rust, with async functions.
 - `examples/rust_kvm`: A simple software KVM which outputs keystrokes/mouse input performed in a window to an emulated HID device (video [here](https://www.youtube.com/watch?v=k16TgXT1ggs)).