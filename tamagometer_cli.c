#include "tamagometer_cli.h"

#include "tamagometer_protocol.h"
#include "tamagometer_version.h"

#include <api_lock.h>
#include <cli/cli.h>
#include <stdio.h>
#include <string.h>

struct TamagometerCli {
  FuriMutex *radio_mutex;
  FuriApiLock active_lock;
};

static bool pipe_cancelled(void *context) {
  return cli_is_pipe_broken_or_is_etx_next_char((PipeSide *)context);
}

static void pipe_progress(TamaTransferStage stage, uint8_t current,
                          uint8_t total, void *context) {
  UNUSED(stage);
  char message[48];
  snprintf(message, sizeof(message), "[TAMAFRIENDS]progress=%u/%u[END]",
           (unsigned int)current, (unsigned int)total);
  pipe_send((PipeSide *)context, (unsigned char *)message, strlen(message));
}

static void pipe_legacy_progress(TamaTransferStage stage, uint8_t current,
                                 uint8_t total, void *context) {
  UNUSED(stage);
  char message[48];
  snprintf(message, sizeof(message), "[TAMALEGACY]progress=%u/%u[END]",
           (unsigned int)current, (unsigned int)total);
  pipe_send((PipeSide *)context, (unsigned char *)message, strlen(message));
}

static void cli_command(PipeSide *pipe, FuriString *args, void *context) {
  TamagometerCli *cli_context = context;
  api_lock_relock(cli_context->active_lock);
  const char *value = furi_string_get_cstr(args);
  char bitstring[161];
  unsigned long outcome;

  if (strcmp(value, "info") == 0) {
    static const unsigned char info_message[] =
        "[TAMAGOMETER]version=" TAMA_APP_VERSION
        ";protocol=" TAMA_PROTOCOL_VERSION ";capabilities=" TAMA_APP_CAPABILITIES "[END]";
    pipe_send(pipe, info_message, sizeof(info_message) - 1U);
  } else if (furi_mutex_acquire(cli_context->radio_mutex, FuriWaitForever) ==
             FuriStatusOk) {
    if (sscanf(value, "send%160s", bitstring) == 1) {
      if (!tama_protocol_ir_send(bitstring))
        printf("Invalid 160-bit message.\r\n");
    } else if (strcmp(value, "listen") == 0) {
      TamaIrReceiveResult result =
          tama_protocol_ir_receive(bitstring, 1000, pipe_cancelled, pipe);
      if (result == TamaIrReceiveSuccess) {
        pipe_send(pipe, (unsigned char *)"[PICO]", 6);
        pipe_send(pipe, (unsigned char *)bitstring, 160);
        pipe_send(pipe, (unsigned char *)"[END]", 5);
      } else if (result == TamaIrReceiveTimeout) {
        static const unsigned char timeout_message[] = "[PICO]timed out[END]";
        pipe_send(pipe, timeout_message, sizeof(timeout_message) - 1U);
      }
    } else if (strcmp(value, "legacy") == 0) {
      TamaLegacySummary summary = {0};
      TamaTransferResult result = tama_protocol_legacy_fallback(
          pipe_cancelled, pipe_legacy_progress, pipe, &summary);
      const char *peer = summary.peer == TamaLegacyPeerV2
                             ? "v2"
                             : summary.peer == TamaLegacyPeerV3 ? "v3"
                             : summary.peer == TamaLegacyPeerV4 ? "v4"
                                                                : "unknown";
      char message[96];
      snprintf(message, sizeof(message),
               "[TAMALEGACY]result=%s;activity=%s;peer=%s[END]",
               tama_protocol_result_text(result),
               tama_protocol_legacy_activity_text(summary.activity), peer);
      pipe_send(pipe, (unsigned char *)message, strlen(message));
    } else if (sscanf(value, "friends%lu", &outcome) == 1 && outcome <= 255) {
      TamaTransferResult result = tama_protocol_friends_transfer(
          (uint8_t)outcome, pipe_cancelled, pipe_progress, pipe);
      if (result == TamaTransferResultSuccess) {
        static const unsigned char ok_message[] = "[TAMAFRIENDS]ok[END]";
        pipe_send(pipe, ok_message, sizeof(ok_message) - 1U);
      } else {
        static const unsigned char cancelled_message[] =
            "[TAMAFRIENDS]cancelled[END]";
        pipe_send(pipe, cancelled_message, sizeof(cancelled_message) - 1U);
      }
    } else {
      printf("Invalid argument(s). Use info, listen, send<bits>, legacy, or "
             "friends<0-255>.\r\n");
    }
    furi_mutex_release(cli_context->radio_mutex);
  }
  api_lock_unlock(cli_context->active_lock);
}

TamagometerCli *tamagometer_cli_alloc(FuriMutex *radio_mutex) {
  TamagometerCli *context = malloc(sizeof(TamagometerCli));
  context->radio_mutex = radio_mutex;
  context->active_lock = api_lock_alloc_locked();
  api_lock_unlock(context->active_lock);
  return context;
}

void tamagometer_cli_register(TamagometerCli *cli_context) {
  CliRegistry *registry = furi_record_open(RECORD_CLI);
  cli_registry_add_command(registry, "tamagometer", CliCommandFlagParallelSafe,
                           cli_command, cli_context);
  furi_record_close(RECORD_CLI);
}

void tamagometer_cli_unregister_and_free(TamagometerCli *cli_context) {
  CliRegistry *registry = furi_record_open(RECORD_CLI);
  cli_registry_delete_command(registry, "tamagometer");
  furi_record_close(RECORD_CLI);
  api_lock_wait_unlock_and_free(cli_context->active_lock);
  free(cli_context);
}
