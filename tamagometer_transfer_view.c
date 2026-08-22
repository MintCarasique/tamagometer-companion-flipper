#include "tamagometer_transfer_view.h"

#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  TamaMode mode;
  TamaTransferStage stage;
  char item_name[34];
  uint8_t current;
  uint8_t total;
  uint8_t animation_frame;
  bool cancellable;
} TransferViewModel;

struct TamagometerTransferView {
  View *view;
  TamagometerTransferCancelCallback cancel_callback;
  void *callback_context;
};

static const char *stage_text(TamaTransferStage stage) {
  switch (stage) {
  case TamaTransferStagePreparing:
    return "Preparing";
  case TamaTransferStageWaitingFirst:
    return "Wait first";
  case TamaTransferStageSendingAck:
    return "Send ack";
  case TamaTransferStageWaitingRequest:
    return "Wait request";
  case TamaTransferStageSendingGift:
    return "Send gift";
  case TamaTransferStageBroadcasting:
    return "BFF broadcast";
  case TamaTransferStageComplete:
    return "Done";
  default:
    return "Working";
  }
}

static void draw_connection_guide(Canvas *canvas, uint8_t frame) {
  canvas_draw_rframe(canvas, 3, 20, 31, 19, 3);
  canvas_draw_str(canvas, 8, 33, "FLIP");
  canvas_draw_rframe(canvas, 94, 20, 31, 19, 8);
  canvas_draw_circle(canvas, 109, 29, 5);
  for (uint8_t beam = 0; beam < 3; beam++) {
    int32_t x = 40 + beam * 16 + ((frame + beam) % 3);
    canvas_draw_line(canvas, x, 26, x + 8, 26);
    canvas_draw_line(canvas, x, 32, x + 8, 32);
  }
  canvas_draw_str_aligned(canvas, 64, 18, AlignCenter, AlignBottom,
                          "Align IR ports");
}

static void draw_friends_guide(Canvas *canvas, uint8_t frame) {
  canvas_draw_rframe(canvas, 23, 21, 42, 20, 3);
  canvas_draw_str(canvas, 31, 35, "FLIP");
  canvas_draw_rframe(canvas, 62, 19, 42, 24, 8);
  canvas_draw_circle(canvas, 83, 31, 6 + (frame % 2));
  canvas_draw_str_aligned(canvas, 64, 18, AlignCenter, AlignBottom,
                          "Back-to-back on LF");
}

static void draw_fitted_title(Canvas *canvas, const char *source) {
  char title[34];
  strlcpy(title, source, sizeof(title));
  size_t length = strlen(title);
  bool shortened = false;
  while (length > 3 && canvas_string_width(canvas, title) > 124) {
    title[--length] = '\0';
    shortened = true;
  }
  if (shortened && length > 3) {
    title[length - 3] = '.';
    title[length - 2] = '.';
    title[length - 1] = '.';
  }
  canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, title);
}

static void transfer_view_draw(Canvas *canvas, void *model_pointer) {
  TransferViewModel *model = model_pointer;
  canvas_clear(canvas);
  canvas_set_font(canvas, FontSecondary);
  draw_fitted_title(canvas, model->item_name);
  if (model->mode == TamaModeFriends) {
    draw_friends_guide(canvas, model->animation_frame);
  } else {
    draw_connection_guide(canvas, model->animation_frame);
  }
  canvas_draw_str_aligned(canvas, 2, 52, AlignLeft, AlignBottom,
                          stage_text(model->stage));
  char progress_text[10];
  snprintf(progress_text, sizeof(progress_text), "%u/%u",
           (unsigned int)model->current, (unsigned int)model->total);
  canvas_draw_str_aligned(canvas, 126, 52, AlignRight, AlignBottom,
                          progress_text);
  float progress =
      model->total ? (float)model->current / (float)model->total : 0.0f;
  elements_progress_bar(canvas, 3, 54, 122, progress);
}

static bool transfer_view_input(InputEvent *event, void *context) {
  TamagometerTransferView *transfer_view = context;
  if ((event->key == InputKeyBack || event->key == InputKeyLeft) &&
      (event->type == InputTypeShort || event->type == InputTypeLong)) {
    if (transfer_view->cancel_callback) {
      transfer_view->cancel_callback(transfer_view->callback_context);
      return true;
    }
  }
  return false;
}

TamagometerTransferView *tamagometer_transfer_view_alloc(void) {
  TamagometerTransferView *transfer_view =
      malloc(sizeof(TamagometerTransferView));
  transfer_view->view = view_alloc();
  transfer_view->cancel_callback = NULL;
  transfer_view->callback_context = NULL;
  view_allocate_model(transfer_view->view, ViewModelTypeLocking,
                      sizeof(TransferViewModel));
  view_set_context(transfer_view->view, transfer_view);
  view_set_draw_callback(transfer_view->view, transfer_view_draw);
  view_set_input_callback(transfer_view->view, transfer_view_input);
  return transfer_view;
}

void tamagometer_transfer_view_free(TamagometerTransferView *transfer_view) {
  view_free(transfer_view->view);
  free(transfer_view);
}

View *
tamagometer_transfer_view_get_view(TamagometerTransferView *transfer_view) {
  return transfer_view->view;
}

void tamagometer_transfer_view_set_cancel_callback(
    TamagometerTransferView *transfer_view,
    TamagometerTransferCancelCallback callback, void *context) {
  transfer_view->cancel_callback = callback;
  transfer_view->callback_context = context;
}

void tamagometer_transfer_view_update(TamagometerTransferView *transfer_view,
                                      TamaMode mode, const char *item_name,
                                      TamaTransferStage stage, uint8_t current,
                                      uint8_t total, uint8_t animation_frame,
                                      bool cancellable) {
  with_view_model(
      transfer_view->view, TransferViewModel * model,
      {
        model->mode = mode;
        model->stage = stage;
        model->current = current;
        model->total = total;
        model->animation_frame = animation_frame;
        model->cancellable = cancellable;
        strlcpy(model->item_name, item_name, sizeof(model->item_name));
      },
      true);
}
