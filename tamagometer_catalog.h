#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  TamaModeConnection,
  TamaModeFriends,
  TamaModeCount,
} TamaMode;

typedef enum {
  TamaCategoryAll,
  TamaCategoryFavorites,
  TamaCategoryRecent,
  TamaCategoryFood,
  TamaCategorySnacks,
  TamaCategoryItemsToys,
  TamaCategoryAnimations,
  TamaCategorySouvenirsSpecial,
  TamaCategoryJewelry,
  TamaCategoryGotchiPoints,
} TamaCategory;

size_t tama_catalog_item_count(TamaMode mode);
uint8_t tama_catalog_item_id(TamaMode mode, size_t index);
const char *tama_catalog_item_name(TamaMode mode, uint8_t item_id, char *buffer,
                                   size_t size);
TamaCategory tama_catalog_item_category(TamaMode mode, uint8_t item_id);
bool tama_catalog_category_matches(TamaMode mode, TamaCategory category,
                                   uint8_t item_id);
const TamaCategory *tama_catalog_categories(TamaMode mode, size_t *count);
const char *tama_catalog_category_name(TamaCategory category);
const char *tama_catalog_mode_name(TamaMode mode);
