/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

#include "UsbToyPad.h"

#if ARDUINO_USB_MODE || !CONFIG_TINYUSB_HID_ENABLED
#error "Select Tools > USB Mode > USB-OTG (TinyUSB) (FQBN option USBMode=default)"
#endif
#if ARDUINO_USB_CDC_ON_BOOT
#error "Select Tools > USB CDC On Boot > Disabled (FQBN option CDCOnBoot=default) so the device only exposes the Toy Pad HID interface"
#endif

// Identical to the descriptor written to functions/hid.g0/report_desc by usb_setup_script.sh
static const uint8_t REPORT_DESCRIPTOR[] = {
  0x06, 0x00, 0xFF, // Usage Page (Vendor Defined 0xFF00)
  0x09, 0x01,       // Usage (0x01)
  0xA1, 0x01,       // Collection (Application)
  0x19, 0x01,       //   Usage Minimum (0x01)
  0x29, 0x20,       //   Usage Maximum (0x20)
  0x15, 0x00,       //   Logical Minimum (0)
  0x26, 0xFF, 0x00, //   Logical Maximum (255)
  0x75, 0x08,       //   Report Size (8)
  0x95, 0x20,       //   Report Count (32)
  0x81, 0x00,       //   Input (Data,Array,Abs)
  0x19, 0x01,       //   Usage Minimum (0x01)
  0x29, 0x20,       //   Usage Maximum (0x20)
  0x91, 0x00,       //   Output (Data,Array,Abs)
  0xC0,             // End Collection
};

static const size_t RX_QUEUE_LENGTH = 16;

UsbToyPad::UsbToyPad() : hid_() {
  static bool registered = false;
  if (!registered) {
    registered = true;
    USBHID::addDevice(this, sizeof(REPORT_DESCRIPTOR));
  }
}

void UsbToyPad::begin() {
  if (!rxQueue_) rxQueue_ = xQueueCreate(RX_QUEUE_LENGTH, ld::REPORT_SIZE);

  USB.VID(0x0E6F);
  USB.PID(0x0241);
  USB.manufacturerName("PDP LIMITED. ");
  USB.productName("LEGO READER V2.10");
  USB.serialNumber("P.D.P.000000");
  USB.firmwareVersion(0x0200);
  // Single interface device: class is defined at interface level (no IAD).
  USB.usbClass(0x00);
  USB.usbSubClass(0x00);
  USB.usbProtocol(0x00);

  hid_.begin();
  USB.begin();
}

uint16_t UsbToyPad::_onGetDescriptor(uint8_t *buffer) {
  memcpy(buffer, REPORT_DESCRIPTOR, sizeof(REPORT_DESCRIPTOR));
  return sizeof(REPORT_DESCRIPTOR);
}

void UsbToyPad::_onOutput(uint8_t /*report_id*/, const uint8_t *buffer, uint16_t len) {
  // Runs in the TinyUSB task: only copy the report, processing happens in loop().
  if (!rxQueue_ || !buffer || !len) return;
  uint8_t report[ld::REPORT_SIZE] = {};
  memcpy(report, buffer, len < ld::REPORT_SIZE ? len : ld::REPORT_SIZE);
  if (xQueueSend(rxQueue_, report, 0) != pdTRUE) log_w("USB receive queue full, dropping report");
}

bool UsbToyPad::receive(uint8_t report[ld::REPORT_SIZE], TickType_t timeout) {
  return rxQueue_ && xQueueReceive(rxQueue_, report, timeout) == pdTRUE;
}

bool UsbToyPad::ready() { return hid_.ready(); }

bool UsbToyPad::send(const uint8_t report[ld::REPORT_SIZE], uint32_t timeoutMs) {
  // Wait until the previous IN transfer has been collected by the host.
  uint32_t start = millis();
  while (!hid_.ready()) {
    if (millis() - start >= timeoutMs) return false;
    delay(1);
  }
  // Report ID 0: the descriptor does not use report IDs, so the 32 bytes are sent as-is.
  return hid_.SendReport(0, report, ld::REPORT_SIZE);
}
