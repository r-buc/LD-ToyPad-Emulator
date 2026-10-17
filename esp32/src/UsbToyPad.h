/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

// TinyUSB HID device that presents itself as the LEGO Dimensions Toy Pad
// ("LEGO READER V2.10", VID 0x0E6F / PID 0x0241) with 32 byte vendor
// defined input/output reports - the same identity the Raspberry Pi version
// configures through usb_setup_script.sh.

#pragma once

#include <Arduino.h>
#include <USB.h>
#include <USBHID.h>

#include <ToyPadProtocol.h>

class UsbToyPad : public USBHIDDevice {
public:
  UsbToyPad();

  // Configures the USB device descriptor and starts the USB stack.
  void begin();

  // Waits up to `timeout` for the next report sent by the game.
  bool receive(uint8_t report[ld::REPORT_SIZE], TickType_t timeout);
  // Sends a 32 byte report to the game. Returns false if the host did not
  // become ready within `timeoutMs` (not connected or previous report not
  // yet collected).
  bool send(const uint8_t report[ld::REPORT_SIZE], uint32_t timeoutMs = 50);
  bool ready();

  // USBHIDDevice
  uint16_t _onGetDescriptor(uint8_t *buffer) override;
  void _onOutput(uint8_t report_id, const uint8_t *buffer, uint16_t len) override;

private:
  USBHID hid_;
  QueueHandle_t rxQueue_ = nullptr;
};
