#pragma once

#include "tamagometer_catalog.h"

#include <stdbool.h>
#include <stdint.h>

#define TAMA_RECENT_LIMIT 12

typedef struct {
  uint8_t mode;
  uint8_t item_id;
} TamaItemRef;

typedef struct {
  bool vibration;
  bool onboarding_complete;
  bool last_valid;
  TamaItemRef last;
  uint8_t connection_favorites[23];
  uint8_t friends_favorites[32];
  uint8_t recent_count;
  TamaItemRef recent[TAMA_RECENT_LIMIT];
} TamaSettings;

void tama_settings_init(TamaSettings *settings);
bool tama_settings_load(TamaSettings *settings);
bool tama_settings_save(const TamaSettings *settings);
bool tama_settings_is_favorite(const TamaSettings *settings, TamaMode mode,
                               uint8_t item_id);
void tama_settings_toggle_favorite(TamaSettings *settings, TamaMode mode,
                                   uint8_t item_id);
void tama_settings_record_transfer(TamaSettings *settings, TamaMode mode,
                                   uint8_t item_id);
bool tama_settings_is_recent(const TamaSettings *settings, TamaMode mode,
                             uint8_t item_id);
bool tama_settings_export_diagnostics(const TamaSettings *settings,
                                      const char *app_version,
                                      const char *last_status);
