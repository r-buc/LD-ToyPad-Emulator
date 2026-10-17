/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

// Glue between the USB Toy Pad, the protocol emulator, the persisted toy tags
// and the web UI. This is the embedded counterpart of index.js.
//
// Threading: USB reports are processed from loop(), HTTP requests arrive on
// the AsyncTCP task. All shared state is guarded by one mutex; notifications
// for the browser and USB transfers happen after the mutex is released, and
// flash writes are deferred to loop() (never in the USB or HTTP callbacks).

#pragma once

#include <Arduino.h>

#include "Catalog.h"
#include "TagStore.h"
#include "ToyPadEmulator.h"

class ToyPadService : public ld::ToyPadListener {
public:
  enum class Result { Ok, BadRequest, NotFound, Conflict, StorageFull };

  // Sends a 32 byte report to the game.
  typedef bool (*ReportSender)(const uint8_t *report);
  // Pushes an event (name + JSON payload) to all connected browsers.
  typedef void (*Notifier)(const char *event, const char *json);

  ToyPadService();

  // Loads the saved toy tags from LittleFS.
  bool begin(ReportSender sender, Notifier notifier);

  // --- USB side, called from loop() ---
  void handleReport(const uint8_t *report, size_t len);
  // Sends queued tag place/remove events to the game.
  void flushEvents();
  // Writes the toy tags to flash if they changed.
  void persist();

  // --- Web side ---
  // On success `uid` receives the UID of the new tag.
  Result createCharacter(uint16_t id, char uid[ld::UID_STRING_SIZE]);
  Result createVehicle(uint16_t id, char uid[ld::UID_STRING_SIZE]);
  Result place(const char *uid, uint8_t pad, uint8_t index);
  Result remove(uint8_t index, const char *uid);
  Result deleteToken(const char *uid);
  void sync();
  // Renders the next part of the toy tags JSON, see TagStore::writeJson.
  size_t writeTagsJson(ld::TagJsonCursor &cursor, uint8_t *buffer, size_t maxLen);
  void statusJson(char *out, size_t len);
  bool connected() const { return connected_; }

  // ToyPadListener (invoked while the mutex is held)
  void onWake() override;
  void onColor(uint8_t pad, const ld::Rgb &color) override;
  void onColorAll(const ld::Rgb colors[3]) override;
  void onFade(uint8_t pad, const ld::Fade &fade) override;
  void onFadeAll(const ld::Fade fades[3]) override;
  void onTagWrite(const ld::Token &token, uint8_t page, const uint8_t data[ld::PAGE_SIZE]) override;

private:
  static constexpr size_t MAX_NOTIFICATIONS = 8;
  struct Notification {
    const char *event;
    char json[80];
  };

  class Lock {
  public:
    explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_); }

  private:
    SemaphoreHandle_t m_;
  };

  void notify(const char *event, const char *json = "null"); // caller must hold mutex_
  void deliverNotifications();
  void newUid(uint8_t uid[ld::UID_SIZE]); // caller must hold mutex_
  Result createEntry(uint16_t id, ld::TagType type, uint32_t upgrades, char uid[ld::UID_STRING_SIZE]);

  SemaphoreHandle_t mutex_;
  ld::ToyPadEmulator emulator_;
  ld::TagStore store_;
  ReportSender sender_ = nullptr;
  Notifier notifier_ = nullptr;
  Notification pending_[MAX_NOTIFICATIONS];
  size_t pendingCount_ = 0;
  volatile bool connected_ = false;
  uint32_t lastSave_ = 0;
};
