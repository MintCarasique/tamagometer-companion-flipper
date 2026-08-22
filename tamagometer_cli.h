#pragma once

#include <furi.h>

typedef struct TamagometerCli TamagometerCli;

TamagometerCli *tamagometer_cli_alloc(FuriMutex *radio_mutex);
void tamagometer_cli_register(TamagometerCli *cli_context);
void tamagometer_cli_unregister_and_free(TamagometerCli *cli_context);
