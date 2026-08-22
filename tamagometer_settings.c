#include "tamagometer_settings.h"

#include <furi.h>
#include <stdio.h>
#include <storage/storage.h>
#include <string.h>

#define TAMA_SETTINGS_MAGIC 0x32474D54UL
#define TAMA_SETTINGS_VERSION 1U

typedef struct {
  uint32_t magic;
  uint8_t version;
  uint8_t vibration;
  uint8_t onboarding_complete;
  uint8_t last_valid;
  TamaItemRef last;
  uint8_t connection_favorites[23];
  uint8_t friends_favorites[32];
  uint8_t recent_count;
  TamaItemRef recent[TAMA_RECENT_LIMIT];
} TamaSettingsFile;

static FuriString *tama_settings_path(Storage *storage, const char *file_name) {
  FuriString *path = furi_string_alloc_printf(APP_DATA_PATH("%s"), file_name);
  storage_common_resolve_path_and_ensure_app_directory(storage, path);
  return path;
}

static bool item_ref_valid(TamaItemRef item) {
  if (item.mode == TamaModeConnection)
    return item.item_id < 181U;
  if (item.mode == TamaModeFriends)
    return item.item_id < 60U || item.item_id >= 0xFBU;
  return false;
}

void tama_settings_init(TamaSettings *settings) {
  memset(settings, 0, sizeof(*settings));
  settings->vibration = true;
}

bool tama_settings_load(TamaSettings *settings) {
  tama_settings_init(settings);
  Storage *storage = furi_record_open(RECORD_STORAGE);
  FuriString *path = tama_settings_path(storage, "settings.bin");
  File *file = storage_file_alloc(storage);
  TamaSettingsFile persisted;
  bool loaded = false;

  if (storage_file_open(file, furi_string_get_cstr(path), FSAM_READ,
                        FSOM_OPEN_EXISTING) &&
      storage_file_read(file, &persisted, sizeof(persisted)) ==
          sizeof(persisted) &&
      persisted.magic == TAMA_SETTINGS_MAGIC &&
      persisted.version == TAMA_SETTINGS_VERSION &&
      persisted.recent_count <= TAMA_RECENT_LIMIT) {
    settings->vibration = persisted.vibration != 0;
    settings->onboarding_complete = persisted.onboarding_complete != 0;
    settings->last_valid = persisted.last_valid != 0;
    settings->last = persisted.last;
    if (!item_ref_valid(settings->last))
      settings->last_valid = false;
    memcpy(settings->connection_favorites, persisted.connection_favorites, 23);
    memcpy(settings->friends_favorites, persisted.friends_favorites, 32);
    for (uint8_t index = 0; index < persisted.recent_count; index++) {
      if (item_ref_valid(persisted.recent[index]))
        settings->recent[settings->recent_count++] = persisted.recent[index];
    }
    loaded = true;
  }

  storage_file_close(file);
  storage_file_free(file);
  furi_string_free(path);
  furi_record_close(RECORD_STORAGE);
  return loaded;
}

bool tama_settings_save(const TamaSettings *settings) {
  TamaSettingsFile persisted = {
      .magic = TAMA_SETTINGS_MAGIC,
      .version = TAMA_SETTINGS_VERSION,
      .vibration = settings->vibration,
      .onboarding_complete = settings->onboarding_complete,
      .last_valid = settings->last_valid,
      .last = settings->last,
      .recent_count = settings->recent_count,
  };
  memcpy(persisted.connection_favorites, settings->connection_favorites, 23);
  memcpy(persisted.friends_favorites, settings->friends_favorites, 32);
  memcpy(persisted.recent, settings->recent, sizeof(settings->recent));

  Storage *storage = furi_record_open(RECORD_STORAGE);
  FuriString *path = tama_settings_path(storage, "settings.bin");
  File *file = storage_file_alloc(storage);
  bool saved = storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE,
                                 FSOM_CREATE_ALWAYS) &&
               storage_file_write(file, &persisted, sizeof(persisted)) ==
                   sizeof(persisted);
  storage_file_close(file);
  storage_file_free(file);
  furi_string_free(path);
  furi_record_close(RECORD_STORAGE);
  return saved;
}

static uint8_t *favorite_bits(TamaSettings *settings, TamaMode mode) {
  return mode == TamaModeFriends ? settings->friends_favorites
                                 : settings->connection_favorites;
}

static const uint8_t *favorite_bits_const(const TamaSettings *settings,
                                          TamaMode mode) {
  return mode == TamaModeFriends ? settings->friends_favorites
                                 : settings->connection_favorites;
}

bool tama_settings_is_favorite(const TamaSettings *settings, TamaMode mode,
                               uint8_t item_id) {
  const uint8_t *bits = favorite_bits_const(settings, mode);
  return (bits[item_id / 8U] & (1U << (item_id % 8U))) != 0;
}

void tama_settings_toggle_favorite(TamaSettings *settings, TamaMode mode,
                                   uint8_t item_id) {
  uint8_t *bits = favorite_bits(settings, mode);
  bits[item_id / 8U] ^= (uint8_t)(1U << (item_id % 8U));
}

void tama_settings_record_transfer(TamaSettings *settings, TamaMode mode,
                                   uint8_t item_id) {
  TamaItemRef item = {.mode = (uint8_t)mode, .item_id = item_id};
  settings->last_valid = true;
  settings->last = item;

  uint8_t existing = settings->recent_count;
  for (uint8_t i = 0; i < settings->recent_count; i++) {
    if (settings->recent[i].mode == item.mode &&
        settings->recent[i].item_id == item.item_id) {
      existing = i;
      break;
    }
  }
  if (existing < settings->recent_count) {
    memmove(&settings->recent[existing], &settings->recent[existing + 1],
            (settings->recent_count - existing - 1U) * sizeof(TamaItemRef));
    settings->recent_count--;
  }
  if (settings->recent_count < TAMA_RECENT_LIMIT)
    settings->recent_count++;
  memmove(&settings->recent[1], &settings->recent[0],
          (settings->recent_count - 1U) * sizeof(TamaItemRef));
  settings->recent[0] = item;
}

bool tama_settings_is_recent(const TamaSettings *settings, TamaMode mode,
                             uint8_t item_id) {
  for (uint8_t i = 0; i < settings->recent_count; i++) {
    if (settings->recent[i].mode == mode &&
        settings->recent[i].item_id == item_id)
      return true;
  }
  return false;
}

bool tama_settings_export_diagnostics(const TamaSettings *settings,
                                      const char *app_version,
                                      const char *last_status) {
  char report[512];
  int length = snprintf(
      report, sizeof(report),
      "Tamagometer Enhanced diagnostic report\n"
      "App version: %s\n"
      "Protocol: 1\n"
      "Capabilities: standalone_ui,connection_ir,friends_lf,hybrid_cli\n"
      "Vibration: %s\n"
      "Onboarding complete: %s\n"
      "Recent transfers: %u\n"
      "Last transfer configured: %s\n"
      "Last runtime status: %s\n",
      app_version, settings->vibration ? "on" : "off",
      settings->onboarding_complete ? "yes" : "no",
      (unsigned int)settings->recent_count, settings->last_valid ? "yes" : "no",
      last_status ? last_status : "none");
  if (length < 0 || (size_t)length >= sizeof(report))
    return false;

  Storage *storage = furi_record_open(RECORD_STORAGE);
  FuriString *path = tama_settings_path(storage, "diagnostics.txt");
  File *file = storage_file_alloc(storage);
  bool saved =
      storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE,
                        FSOM_CREATE_ALWAYS) &&
      storage_file_write(file, report, (size_t)length) == (size_t)length;
  storage_file_close(file);
  storage_file_free(file);
  furi_string_free(path);
  furi_record_close(RECORD_STORAGE);
  return saved;
}
