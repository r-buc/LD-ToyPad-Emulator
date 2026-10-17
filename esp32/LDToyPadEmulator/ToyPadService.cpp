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
const char *TOYTAGS_PATH = "/json/toytags.json";
const char *TOYTAGS_TMP_PATH = "/json/toytags.json.tmp";
const char *TOYTAGS_BACKUP_PATH = "/json/toytags.json.bak";
const char *CHARACTERS_PATH = "/json/charactermap.json";
const char *VEHICLES_PATH = "/json/tokenmap.json";

constexpr uint32_t NEW_VEHICLE_UPGRADES = 0xefffffff;
// Keeps toytags.json (and the RAM needed to serialise it) bounded.
constexpr size_t MAX_TAGS = 400;

String quoted(const std::string &s) { return String("\"") + s.c_str() + "\""; }

String color(const ld::Rgb &c) { return quoted(ld::displayColor(c.r, c.g, c.b)); }

bool loadCatalog(ld::Catalog &catalog, const char *path) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    log_e("Missing %s - did you upload the filesystem image (see README: upload the LittleFS image)?", path);
    return false;
  }
  bool ok = catalog.load(f);
  f.close();
  if (!ok) log_e("Failed to parse %s", path);
  return ok;
}
} // namespace

ToyPadService::ToyPadService() : mutex_(xSemaphoreCreateMutex()) { emulator_.setListener(this); }

bool ToyPadService::begin(ReportSender sender, Notifier notifier) {
  sender_ = std::move(sender);
  notifier_ = std::move(notifier);

  bool ok = loadCatalog(characters_, CHARACTERS_PATH);
  ok = loadCatalog(vehicles_, VEHICLES_PATH) && ok;
  log_i("Catalog: %u characters, %u vehicles", characters_.size(), vehicles_.size());

  Lock lock(mutex_);
  File f = LittleFS.open(TOYTAGS_PATH, "r");
  if (!f) {
    log_w("%s not found, starting with an empty toy box", TOYTAGS_PATH);
    store_.markDirty();
  } else {
    bool parsed = store_.load(f);
    f.close();
    if (!parsed) {
      log_e("%s is corrupt, moving it to %s", TOYTAGS_PATH, TOYTAGS_BACKUP_PATH);
      LittleFS.remove(TOYTAGS_BACKUP_PATH);
      LittleFS.rename(TOYTAGS_PATH, TOYTAGS_BACKUP_PATH);
      store_.markDirty();
    }
  }
  // Nothing is on the pad after a restart (same as initializeToyTagsJSON()).
  store_.resetIndexes();
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
  String json;
  {
    Lock lock(mutex_);
    if (!store_.dirty()) return;
    store_.save(json);
    store_.clearDirty();
  }
  lastSave_ = millis();

  bool ok = false;
  File f = LittleFS.open(TOYTAGS_TMP_PATH, "w");
  if (f) {
    ok = f.print(json) == json.length();
    f.close();
  }
  if (ok && !LittleFS.rename(TOYTAGS_TMP_PATH, TOYTAGS_PATH)) {
    LittleFS.remove(TOYTAGS_PATH);
    ok = LittleFS.rename(TOYTAGS_TMP_PATH, TOYTAGS_PATH);
  }
  if (!ok) {
    log_e("Failed to write %s", TOYTAGS_PATH);
    Lock lock(mutex_);
    store_.markDirty(); // retry later
  } else {
    log_d("Saved %s (%u bytes)", TOYTAGS_PATH, json.length());
  }
}

std::string ToyPadService::newUid() const {
  std::string uid;
  do {
    uid = ld::randomUid(esp_random);
  } while (store_.find(uid));
  return uid;
}

std::string ToyPadService::anyName(uint16_t id) const {
  // Characters use ids below 1000, vehicles and gadgets start at 1000.
  return id >= 1000 ? vehicles_.name(id) : characters_.name(id);
}

ToyPadService::Result ToyPadService::createEntry(uint16_t id, const char *type, uint32_t upgrades, std::string &uid) {
  const bool character = strcmp(type, "character") == 0;
  const ld::Catalog &catalog = character ? characters_ : vehicles_;
  if (!catalog.contains(id)) return Result::BadRequest;

  Lock lock(mutex_);
  if (store_.size() >= MAX_TAGS) return Result::StorageFull;
  ld::TagEntry entry;
  entry.name = catalog.name(id);
  entry.id = id;
  entry.uid = newUid();
  entry.index = -1;
  entry.type = type;
  entry.vehicleUpgradesP23 = upgrades;
  entry.vehicleUpgradesP25 = upgrades;
  store_.add(entry);
  uid = entry.uid;
  log_i("Created %s %u (%s) uid %s", type, id, entry.name.c_str(), uid.c_str());
  return Result::Ok;
}

ToyPadService::Result ToyPadService::createCharacter(uint16_t id, std::string &uid) {
  return createEntry(id, "character", 0, uid);
}

ToyPadService::Result ToyPadService::createVehicle(uint16_t id, std::string &uid) {
  return createEntry(id, "vehicle", NEW_VEHICLE_UPGRADES, uid);
}

ToyPadService::Result ToyPadService::place(const std::string &uid, uint8_t pad, uint8_t index) {
  uint8_t uidBytes[ld::UID_SIZE];
  if (!ld::parseUid(uid.c_str(), uidBytes)) return Result::BadRequest;

  Result result;
  {
    Lock lock(mutex_);
    const ld::TagEntry *entry = store_.find(uid);
    if (!entry) return Result::NotFound;

    for (uint8_t i = 1; i <= ld::MAX_PAD_INDEX; i++) {
      const ld::Token *t = emulator_.tokenAt(i);
      if (t && memcmp(t->uid, uidBytes, ld::UID_SIZE) == 0) return Result::Conflict; // already on the pad
    }

    ld::Token token = entry->isCharacter()
                        ? ld::makeCharacterToken(entry->id, uidBytes)
                        : ld::makeVehicleToken(entry->id, uidBytes, entry->vehicleUpgradesP23, entry->vehicleUpgradesP25);
    if (emulator_.place(token, pad, index)) {
      store_.setIndex(uid, index);
      log_i("Placed %s (%s) on pad %u index %u", uid.c_str(), entry->name.c_str(), pad, index);
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
  std::string tokenUid = token ? ld::formatUid(token->uid) : std::string();
  bool removed = emulator_.remove(index);
  if (!tokenUid.empty()) store_.setIndex(tokenUid, -1);
  if (!uid.empty()) store_.setIndex(uid, -1);
  log_i("Removed index %u (%s)", index, removed ? tokenUid.c_str() : "empty");
  return Result::Ok;
}

ToyPadService::Result ToyPadService::deleteToken(const std::string &uid) {
  {
    Lock lock(mutex_);
    const ld::TagEntry *entry = store_.find(uid);
    if (!entry) return Result::NotFound;
    const ld::Token *token = entry->index > 0 ? emulator_.tokenAt(static_cast<uint8_t>(entry->index)) : nullptr;
    if (token && ld::formatUid(token->uid) == uid) emulator_.remove(token->index);
    store_.erase(uid);
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
      if (token) store_.setIndex(ld::formatUid(token->uid), i);
    }
    notify("refreshTokens");
  }
  deliverNotifications();
}

String ToyPadService::toyTagsJson() {
  String json;
  Lock lock(mutex_);
  store_.save(json);
  return json;
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
  std::string uid = ld::formatUid(token.uid);
  ld::TagEntry *entry = store_.find(uid);
  log_i("REQUEST (CMD_WRITE): index %u page %u uid %s", token.index, page, uid.c_str());
  if (!entry) return;

  if (page == 24 || page == 36) {
    // The game rebuilt the vehicle into another model: page 0x24 holds the id.
    uint16_t id = ld::readU16LE(data);
    entry->id = id;
    entry->name = anyName(id);
    entry->type = "vehicle";
    store_.markDirty();
  } else if (page == 23 || page == 35) {
    entry->vehicleUpgradesP23 = ld::readU32LE(data);
    store_.markDirty();
  } else if (page == 25 || page == 37) {
    entry->vehicleUpgradesP25 = ld::readU32LE(data);
    store_.markDirty();
    notify("refreshTokens"); // the last page of a write sequence
  }
}
