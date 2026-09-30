#pragma once

#include <gui/canvas.h>

/* Compact device silhouettes: Flipper 41x22, Tamagotchi 22x24 (24x22 sideways). */
void tama_draw_flipper(Canvas *canvas, int32_t x, int32_t y, bool back);
void tama_draw_tamagotchi(Canvas *canvas, int32_t x, int32_t y,
                          bool back, bool sideways);
