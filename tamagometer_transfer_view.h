#pragma once

#include "tamagometer_catalog.h"
#include "tamagometer_protocol.h"

#include <gui/view.h>

typedef struct TamagometerTransferView TamagometerTransferView;
typedef void (*TamagometerTransferCancelCallback)(void *context);

TamagometerTransferView *tamagometer_transfer_view_alloc(void);
void tamagometer_transfer_view_free(TamagometerTransferView *transfer_view);
View *
tamagometer_transfer_view_get_view(TamagometerTransferView *transfer_view);
void tamagometer_transfer_view_set_cancel_callback(
    TamagometerTransferView *transfer_view,
    TamagometerTransferCancelCallback callback, void *context);
void tamagometer_transfer_view_update(TamagometerTransferView *transfer_view,
                                      TamaMode mode, const char *item_name,
                                      TamaTransferStage stage, uint8_t current,
                                      uint8_t total, uint8_t animation_frame,
                                      bool cancellable);
