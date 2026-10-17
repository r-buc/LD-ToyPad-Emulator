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
// Bounds RAM (20 bytes per tag) and the size of the toy box in the web UI.
constexpr size_t MAX_TAGS = 400;

String quoted(const std::string &s) { return String("\"") + s.c_str() + "\""; }

String color(const ld::Rgb &c) { return quoted(ld::displayColor(c.r, c.g, c.b)); }
} // namespace

ToyPadService::ToyPadService() : mutex_(xSemaphoreCreateMutex()) { emulator_.setListener(this); }

bool ToyPadService::begin(ReportSender sender, Notifier notifier) {
  sender_ = std::move(sender);
  notifier_ = std::move(notifier);

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
  std::vector<ld::TagEntry> snapshot;
  {
    Lock lock(mutex_);
    if (!store_.dirty()) return;
    snapshot = store_.entries();
    store_.clearDirty();
  }
  lastSave_ = millis();

  bool ok = false;
  File f = LittleFS.open(TAGS_TMP_PATH, "w");
  if (f) {
    ok = ld::TagStore::save(snapshot, f);
    f.close();
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
    log_d("Saved %u tags to %s", snapshot.size(), TAGS_PATH);
  }
}

void ToyPadService::newUid(uint8_t uid[ld::UID_SIZE]) {
  do {
    ld::randomUid(esp_random, uid);
  } while (store_.find(uid));
}

ToyPadService::Result ToyPadService::createEntry(uint16_t id, ld::TagType type, uint32_t upgrades, std::string &uid) {
  const ld::CatalogEntry *item = type == ld::TagType::Character ? ld::findCharacter(id) : ld::findVehicle(id);
  if (!item) return Result::BadRequest;

  Lock lock(mutex_);
  if (store_.size() >= MAX_TAGS) return Result::StorageFull;
  ld::TagEntry entry;
  newUid(entry.uid);
  entry.type = type;
  entry.id = id;
  entry.upgradesP23 = upgrades;
  entry.upgradesP25 = upgrades;
  store_.add(entry);
  uid = ld::formatUid(entry.uid);
  log_i("Created %u (%s) uid %s", id, item->name, uid.c_str());
  return Result::Ok;
}

ToyPadService::Result ToyPadService::createCharacter(uint16_t id, std::string &uid) {
  return createEntry(id, ld::TagType::Character, 0, uid);
}

ToyPadService::Result ToyPadService::createVehicle(uint16_t id, std::string &uid) {
  return createEntry(id, ld::TagType::Vehicle, NEW_VEHICLE_UPGRADES, uid);
}

ToyPadService::Result ToyPadService::place(const std::string &uid, uint8_t pad, uint8_t index) {
  uint8_t uidBytes[ld::UID_SIZE];
  if (!ld::parseUid(uid.c_str(), uidBytes)) return Result::BadRequest;

  Result result;
  {
    Lock lock(mutex_);
    const ld::TagEntry *entry = store_.find(uidBytes);
    if (!entry) return Result::NotFound;

    for (uint8_t i = 1; i <= ld::MAX_PAD_INDEX; i++) {
      const ld::Token *t = emulator_.tokenAt(i);
      if (t && memcmp(t->uid, uidBytes, ld::UID_SIZE) == 0) return Result::Conflict; // already on the pad
    }

    ld::Token token = entry->type == ld::TagType::Character
                        ? ld::makeCharacterToken(entry->id, uidBytes)
                        : ld::makeVehicleToken(entry->id, uidBytes, entry->upgradesP23, entry->upgradesP25);
    if (emulator_.place(token, pad, index)) {
      store_.setIndex(uidBytes, index);
      log_i("Placed %s (%s) on pad %u index %u", uid.c_str(), ld::catalogName(entry->id), pad, index);
      result = Result::Ok;
    } else {
      result = Result::Conflict;
    }
  }
  return result;
}

ToyPadService::Result ToyPadService::remove(uint8_t index, const std::string &uid) {
  Lock lock(mutex_);
  const ld::Token *token = emulator_.tokenAt(index);
  if (token) {
    store_.setIndex(token->uid, -1);
    log_i("Removed index %u (%s)", index, ld::formatUid(token->uid).c_str());
    emulator_.remove(index);
  }
  uint8_t uidBytes[ld::UID_SIZE];
  if (ld::parseUid(uid.c_str(), uidBytes)) store_.setIndex(uidBytes, -1);
  return Result::Ok;
}

ToyPadService::Result ToyPadService::deleteToken(const std::string &uid) {
  uint8_t uidBytes[ld::UID_SIZE];
  if (!ld::parseUid(uid.c_str(), uidBytes)) return Result::BadRequest;
  {
    Lock lock(mutex_);
    const ld::TagEntry *entry = store_.find(uidBytes);
    if (!entry) return Result::NotFound;
    const ld::Token *token = entry->index > 0 ? emulator_.tokenAt(static_cast<uint8_t>(entry->index)) : nullptr;
    if (token && memcmp(token->uid, uidBytes, ld::UID_SIZE) == 0) emulator_.remove(token->index);
    store_.erase(uidBytes);
    log_i("Deleted %s", uid.c_str());
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

std::vector<ld::TagEntry> ToyPadService::tags() {
  Lock lock(mutex_);
  return store_.entries();
}

String ToyPadService::statusJson() {
  size_t tokens = 0, tags = 0;
  {
    Lock lock(mutex_);
    for (uint8_t i = 1; i <= ld::MAX_PAD_INDEX; i++)
      if (emulator_.tokenAt(i)) tokens++;
    tags = store_.size();
  }
  return String("{\"connected\":") + (connected_ ? "true" : "false") + ",\"tokensOnPad\":" + tokens + ",\"tags\":" + tags +
         ",\"freeHeap\":" + ESP.getFreeHeap() + ",\"uptimeMs\":" + millis() + "}";
}

void ToyPadService::notify(const char *event, const String &json) { pending_.push_back({event, json}); }

void ToyPadService::deliverNotifications() {
  std::vector<Notification> notifications;
  {
    Lock lock(mutex_);
    notifications.swap(pending_);
  }
  if (!notifier_) return;
  for (const auto &n : notifications) notifier_(n.event, n.json);
}

void ToyPadService::onWake() {
  log_i("REQUEST (CMD_WAKE): game connected");
  connected_ = true;
  notify("Connection True");
}

void ToyPadService::onColor(uint8_t pad, const ld::Rgb &c) {
  if (pad == 0)
    notify("Color All", "[" + color(c) + "," + color(c) + "," + color(c) + "]");
  else
    notify("Color One", String("[") + pad + "," + color(c) + "]");
}

void ToyPadService::onColorAll(const ld::Rgb colors[3]) {
  notify("Color All", "[" + color(colors[0]) + "," + color(colors[1]) + "," + color(colors[2]) + "]");
}

void ToyPadService::onFade(uint8_t pad, const ld::Fade &fade) {
  notify("Fade One", String("[") + pad + "," + fade.speed + "," + fade.cycles + "," + color(fade.color) + "]");
}

void ToyPadService::onFadeAll(const ld::Fade fades[3]) {
  String json = "[";
  for (int i = 0; i < 3; i++) {
    if (i) json += ",";
    json += String(fades[i].speed) + "," + fades[i].cycles + "," + color(fades[i].color);
  }
  json += "]";
  notify("Fade All", json);
}

void ToyPadService::onTagWrite(const ld::Token &token, uint8_t page, const uint8_t data[ld::PAGE_SIZE]) {
  // Vehicle data lives in pages 0x23-0x25 (35-37); like index.js the pages
  // 12 below (23-25) are accepted as well.
  ld::TagEntry *entry = store_.find(token.uid);
  log_i("REQUEST (CMD_WRITE): index %u page %u uid %s", token.index, page, ld::formatUid(token.uid).c_str());
  if (!entry) return;

  if (page == 24 || page == 36) {
    // The game rebuilt the vehicle into another model: page 0x24 holds the id.
    entry->id = ld::readU16LE(data);
    entry->type = ld::TagType::Vehicle;
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
