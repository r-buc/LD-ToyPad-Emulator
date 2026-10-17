/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

#include "ToyPadService.h"

#include "config.h"

#include <LittleFS.h>
#include <esp_random.h>

namespace {
const char *TAGS_PATH = "/toytags.bin";
const char *TAGS_TMP_PATH = "/toytags.bin.tmp";
const char *TAGS_BACKUP_PATH = "/toytags.bin.bak";

constexpr uint32_t NEW_VEHICLE_UPGRADES = 0xefffffff;
// Tags encoded per flash write in persist().
constexpr size_t SAVE_BATCH = 16;

struct Color {
  char hex[ld::COLOR_STRING_SIZE];
  explicit Color(const ld::Rgb &c) { ld::displayColor(c.r, c.g, c.b, hex); }
};
} // namespace

ToyPadService::ToyPadService() : mutex_(xSemaphoreCreateMutex()) { emulator_.setListener(this); }

bool ToyPadService::begin(ReportSender sender, Notifier notifier) {
  sender_ = sender;
  notifier_ = notifier;

  Lock lock(mutex_);
  File f = LittleFS.open(TAGS_PATH, "r");
  if (!f) {
    log_w("%s not found, starting with an empty toy box", TAGS_PATH);
    return true;
  }
  bool ok = store_.load(f);
  f.close();
  if (!ok) {
    log_e("%s is corrupt, moving it to %s", TAGS_PATH, TAGS_BACKUP_PATH);
    LittleFS.remove(TAGS_BACKUP_PATH);
    LittleFS.rename(TAGS_PATH, TAGS_BACKUP_PATH);
    store_.markDirty();
  }
  log_i("Loaded %u toy tags", store_.size());
  return ok;
}

void ToyPadService::handleReport(const uint8_t *report, size_t len) {
  uint8_t response[ld::REPORT_SIZE];
  bool hasResponse;
  {
    Lock lock(mutex_);
    hasResponse = emulator_.handleReport(report, len, response);
  }
  if (hasResponse && sender_ && !sender_(response)) log_w("Failed to send response for command 0x%02x", report[2]);
  deliverNotifications();
}

void ToyPadService::flushEvents() {
  uint8_t event[ld::REPORT_SIZE];
  for (;;) {
    {
      Lock lock(mutex_);
      if (!emulator_.peekEvent(event)) break;
    }
    if (!sender_ || !sender_(event)) break; // keep the event, retry on the next flush
    {
      Lock lock(mutex_);
      emulator_.popEvent(event);
    }
    log_d("EVENT: pad %u index %u %s", event[2], event[4], event[5] ? "removed" : "placed");
  }
}

void ToyPadService::persist() {
  if (millis() - lastSave_ < TOYPAD_SAVE_INTERVAL_MS) return;
  {
    Lock lock(mutex_);
    if (!store_.dirty()) return;
    store_.clearDirty();
  }
  lastSave_ = millis();

  // The tags are encoded in small batches, so the mutex is never held during
  // flash writes. If they change in between (dirty again) the save is
  // abandoned and repeated after the next interval.
  File f = LittleFS.open(TAGS_TMP_PATH, "w");
  uint8_t buffer[SAVE_BATCH * ld::TagStore::RECORD_SIZE];
  ld::TagStore::writeHeader(buffer);
  bool ok = f && f.write(buffer, ld::TagStore::HEADER_SIZE) == ld::TagStore::HEADER_SIZE;
  bool changed = false;
  size_t saved = 0;
  while (ok) {
    size_t n;
    {
      Lock lock(mutex_);
      changed = store_.dirty();
      n = changed ? 0 : store_.encode(saved, buffer, SAVE_BATCH);
    }
    if (n == 0) break;
    ok = f.write(buffer, n * ld::TagStore::RECORD_SIZE) == n * ld::TagStore::RECORD_SIZE;
    saved += n;
  }
  if (f) f.close();

  if (ok && changed) {
    LittleFS.remove(TAGS_TMP_PATH);
    return;
  }
  if (ok && !LittleFS.rename(TAGS_TMP_PATH, TAGS_PATH)) {
    LittleFS.remove(TAGS_PATH);
    ok = LittleFS.rename(TAGS_TMP_PATH, TAGS_PATH);
  }
  if (!ok) {
    log_e("Failed to write %s", TAGS_PATH);
    Lock lock(mutex_);
    store_.markDirty(); // retry later
  } else {
    log_d("Saved %u tags to %s", saved, TAGS_PATH);
  }
}

void ToyPadService::newUid(uint8_t uid[ld::UID_SIZE]) {
  do {
    ld::randomUid(esp_random, uid);
  } while (store_.find(uid));
}

ToyPadService::Result ToyPadService::createEntry(uint16_t id, ld::TagType type, uint32_t upgrades,
                                                 char uid[ld::UID_STRING_SIZE]) {
  const ld::CatalogEntry *item = type == ld::TAG_CHARACTER ? ld::findCharacter(id) : ld::findVehicle(id);
  if (!item) return Result::BadRequest;

  Lock lock(mutex_);
  uint8_t uidBytes[ld::UID_SIZE];
  newUid(uidBytes);
  ld::TagEntry *entry = store_.add();
  if (!entry) return Result::StorageFull;
  memcpy(entry->uid, uidBytes, ld::UID_SIZE);
  entry->type = type;
  entry->id = id;
  entry->upgradesP23 = upgrades;
  entry->upgradesP25 = upgrades;
  ld::formatUid(entry->uid, uid);
  log_i("Created %u (%s) uid %s", id, item->name, uid);
  return Result::Ok;
}

ToyPadService::Result ToyPadService::createCharacter(uint16_t id, char uid[ld::UID_STRING_SIZE]) {
  return createEntry(id, ld::TAG_CHARACTER, 0, uid);
}

ToyPadService::Result ToyPadService::createVehicle(uint16_t id, char uid[ld::UID_STRING_SIZE]) {
  return createEntry(id, ld::TAG_VEHICLE, NEW_VEHICLE_UPGRADES, uid);
}

ToyPadService::Result ToyPadService::place(const char *uid, uint8_t pad, uint8_t index) {
  uint8_t uidBytes[ld::UID_SIZE];
  if (!ld::parseUid(uid, uidBytes)) return Result::BadRequest;

  Result result;
  {
    Lock lock(mutex_);
    const ld::TagEntry *entry = store_.find(uidBytes);
    if (!entry) return Result::NotFound;

    for (uint8_t i = 1; i <= ld::MAX_PAD_INDEX; i++) {
      const ld::Token *t = emulator_.tokenAt(i);
      if (t && memcmp(t->uid, uidBytes, ld::UID_SIZE) == 0) return Result::Conflict; // already on the pad
    }

    ld::Token token = entry->type == ld::TAG_CHARACTER
                        ? ld::makeCharacterToken(entry->id, uidBytes)
                        : ld::makeVehicleToken(entry->id, uidBytes, entry->upgradesP23, entry->upgradesP25);
    if (emulator_.place(token, pad, index)) {
      store_.setIndex(uidBytes, index);
      log_i("Placed %s (%s) on pad %u index %u", uid, ld::catalogName(entry->id), pad, index);
      result = Result::Ok;
    } else {
      result = Result::Conflict;
    }
  }
  return result;
}

ToyPadService::Result ToyPadService::remove(uint8_t index, const char *uid) {
  Lock lock(mutex_);
  const ld::Token *token = emulator_.tokenAt(index);
  if (token) {
    store_.setIndex(token->uid, -1);
    log_i("Removed index %u", index);
    emulator_.remove(index);
  }
  uint8_t uidBytes[ld::UID_SIZE];
  if (ld::parseUid(uid, uidBytes)) store_.setIndex(uidBytes, -1);
  return Result::Ok;
}

ToyPadService::Result ToyPadService::deleteToken(const char *uid) {
  uint8_t uidBytes[ld::UID_SIZE];
  if (!ld::parseUid(uid, uidBytes)) return Result::BadRequest;
  {
    Lock lock(mutex_);
    const ld::TagEntry *entry = store_.find(uidBytes);
    if (!entry) return Result::NotFound;
    const ld::Token *token = entry->index > 0 ? emulator_.tokenAt(static_cast<uint8_t>(entry->index)) : nullptr;
    if (token && memcmp(token->uid, uidBytes, ld::UID_SIZE) == 0) emulator_.remove(token->index);
    store_.erase(uidBytes);
    log_i("Deleted %s", uid);
    notify("refreshTokens");
  }
  deliverNotifications();
  return Result::Ok;
}

void ToyPadService::sync() {
  {
    Lock lock(mutex_);
    store_.resetIndexes();
    for (uint8_t i = 1; i <= ld::MAX_PAD_INDEX; i++) {
      const ld::Token *token = emulator_.tokenAt(i);
      if (token) store_.setIndex(token->uid, i);
    }
    notify("refreshTokens");
  }
  deliverNotifications();
}

size_t ToyPadService::writeTagsJson(ld::TagJsonCursor &cursor, uint8_t *buffer, size_t maxLen) {
  Lock lock(mutex_);
  return store_.writeJson(cursor, buffer, maxLen);
}

void ToyPadService::statusJson(char *out, size_t len) {
  size_t tokens = 0, tags = 0;
  {
    Lock lock(mutex_);
    for (uint8_t i = 1; i <= ld::MAX_PAD_INDEX; i++)
      if (emulator_.tokenAt(i)) tokens++;
    tags = store_.size();
  }
  snprintf(out, len, "{\"connected\":%s,\"tokensOnPad\":%u,\"tags\":%u,\"freeHeap\":%u,\"uptimeMs\":%u}",
           connected_ ? "true" : "false", static_cast<unsigned>(tokens), static_cast<unsigned>(tags),
           static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(millis()));
}

void ToyPadService::notify(const char *event, const char *json) {
  if (pendingCount_ >= MAX_NOTIFICATIONS) {
    log_w("Notification queue full, dropping '%s'", event);
    return;
  }
  Notification &n = pending_[pendingCount_++];
  n.event = event;
  strlcpy(n.json, json, sizeof(n.json));
}

void ToyPadService::deliverNotifications() {
  Notification notifications[MAX_NOTIFICATIONS];
  size_t count;
  {
    Lock lock(mutex_);
    count = pendingCount_;
    memcpy(notifications, pending_, count * sizeof(Notification));
    pendingCount_ = 0;
  }
  if (!notifier_) return;
  for (size_t i = 0; i < count; i++) notifier_(notifications[i].event, notifications[i].json);
}

void ToyPadService::onWake() {
  log_i("REQUEST (CMD_WAKE): game connected");
  connected_ = true;
  notify("Connection True");
}

void ToyPadService::onColor(uint8_t pad, const ld::Rgb &c) {
  char json[sizeof(Notification::json)];
  Color hex(c);
  if (pad == 0) {
    snprintf(json, sizeof(json), "[\"%s\",\"%s\",\"%s\"]", hex.hex, hex.hex, hex.hex);
    notify("Color All", json);
  } else {
    snprintf(json, sizeof(json), "[%u,\"%s\"]", pad, hex.hex);
    notify("Color One", json);
  }
}

void ToyPadService::onColorAll(const ld::Rgb colors[3]) {
  char json[sizeof(Notification::json)];
  snprintf(json, sizeof(json), "[\"%s\",\"%s\",\"%s\"]", Color(colors[0]).hex, Color(colors[1]).hex, Color(colors[2]).hex);
  notify("Color All", json);
}

void ToyPadService::onFade(uint8_t pad, const ld::Fade &fade) {
  char json[sizeof(Notification::json)];
  snprintf(json, sizeof(json), "[%u,%u,%u,\"%s\"]", pad, fade.speed, fade.cycles, Color(fade.color).hex);
  notify("Fade One", json);
}

void ToyPadService::onFadeAll(const ld::Fade fades[3]) {
  char json[sizeof(Notification::json)];
  snprintf(json, sizeof(json), "[%u,%u,\"%s\",%u,%u,\"%s\",%u,%u,\"%s\"]", fades[0].speed, fades[0].cycles,
           Color(fades[0].color).hex, fades[1].speed, fades[1].cycles, Color(fades[1].color).hex, fades[2].speed,
           fades[2].cycles, Color(fades[2].color).hex);
  notify("Fade All", json);
}

void ToyPadService::onTagWrite(const ld::Token &token, uint8_t page, const uint8_t data[ld::PAGE_SIZE]) {
  // Vehicle data lives in pages 0x23-0x25 (35-37); like index.js the pages
  // 12 below (23-25) are accepted as well.
  ld::TagEntry *entry = store_.find(token.uid);
  log_i("REQUEST (CMD_WRITE): index %u page %u", token.index, page);
  if (!entry) return;

  if (page == 24 || page == 36) {
    // The game rebuilt the vehicle into another model: page 0x24 holds the id.
    entry->id = ld::readU16LE(data);
    entry->type = ld::TAG_VEHICLE;
    store_.markDirty();
  } else if (page == 23 || page == 35) {
    entry->upgradesP23 = ld::readU32LE(data);
    store_.markDirty();
  } else if (page == 25 || page == 37) {
    entry->upgradesP25 = ld::readU32LE(data);
    store_.markDirty();
    notify("refreshTokens"); // the last page of a write sequence
  }
}
