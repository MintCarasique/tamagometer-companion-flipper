#include "tamagometer_device_icons.h"
#include <furi.h>

typedef struct {
  uint8_t x;
  uint8_t y;
} DevicePoint;

static DevicePoint orient(DevicePoint point, bool sideways) {
  if (sideways) {
    return (DevicePoint){23U - point.y, point.x};
  }
  return point;
}

static void tama_line(Canvas *canvas, int32_t x, int32_t y,
                      DevicePoint start, DevicePoint end, bool sideways) {
  start = orient(start, sideways);
  end = orient(end, sideways);
  canvas_draw_line(canvas, x + start.x, y + start.y, x + end.x, y + end.y);
}

void tama_draw_tamagotchi(Canvas *canvas, int32_t x, int32_t y,
                          bool back, bool sideways) {
  static const DevicePoint shell[] = {
      {11, 0}, {15, 2}, {18, 6}, {21, 13}, {21, 17}, {19, 21},
      {15, 23}, {7, 23}, {3, 21}, {1, 17}, {1, 13}, {4, 6}, {7, 2},
  };
  for (size_t i = 0; i < COUNT_OF(shell); i++) {
    tama_line(canvas, x, y, shell[i], shell[(i + 1U) % COUNT_OF(shell)], sideways);
  }
  tama_line(canvas, x, y, (DevicePoint){9, 1}, (DevicePoint){13, 1}, sideways);
  if (back) {
    canvas_draw_rframe(canvas, x + 5, y + 8, 13, 12, 3);
    canvas_draw_dot(canvas, x + 11, y + 10);
    canvas_draw_circle(canvas, x + 11, y + 15, 3);
    return;
  }
  static const DevicePoint display[] = {{5, 8}, {17, 8}, {17, 17}, {5, 17}};
  for (size_t i = 0; i < COUNT_OF(display); i++) {
    tama_line(canvas, x, y, display[i], display[(i + 1U) % COUNT_OF(display)], sideways);
  }
  tama_line(canvas, x, y, (DevicePoint){8, 11}, (DevicePoint){8, 12}, sideways);
  tama_line(canvas, x, y, (DevicePoint){14, 11}, (DevicePoint){14, 12}, sideways);
  tama_line(canvas, x, y, (DevicePoint){10, 14}, (DevicePoint){12, 14}, sideways);
  for (uint8_t button = 0; button < 3; button++) {
    DevicePoint center = orient((DevicePoint){6 + 5 * button, 20}, sideways);
    canvas_draw_disc(canvas, x + center.x, y + center.y, 1);
  }
}

void tama_draw_flipper(Canvas *canvas, int32_t x, int32_t y, bool back) {
  canvas_draw_rframe(canvas, x, y, 41, 22, 5);
  canvas_draw_box(canvas, x, y + 7, 3, 8);
  if (back) {
    canvas_draw_rframe(canvas, x + 8, y + 4, 26, 14, 4);
    canvas_draw_circle(canvas, x + 20, y + 11, 3);
    canvas_draw_circle(canvas, x + 20, y + 11, 6);
    return;
  }
  canvas_draw_rframe(canvas, x + 6, y + 5, 18, 12, 2);
  canvas_draw_line(canvas, x + 10, y + 11, x + 13, y + 9);
  canvas_draw_line(canvas, x + 13, y + 9, x + 19, y + 9);
  canvas_draw_line(canvas, x + 14, y + 10, x + 18, y + 13);
  canvas_draw_rframe(canvas, x + 28, y + 4, 10, 14, 3);
  canvas_draw_box(canvas, x + 32, y + 7, 2, 8);
  canvas_draw_box(canvas, x + 29, y + 10, 8, 2);
  canvas_draw_dot(canvas, x + 26, y + 15);
}
