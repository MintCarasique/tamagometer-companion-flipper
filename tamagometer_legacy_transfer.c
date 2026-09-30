#include "tamagometer_protocol.h"
#include "tamagometer_legacy.h"

#include <furi.h>
#include <furi_hal_infrared.h>
#include <infrared_worker.h>
#include <string.h>

#define LEGACY_MAX_BYTES TAMA_LEGACY_MAX_BYTES
#define LEGACY_MAX_TIMINGS 323U

typedef struct {
  volatile bool received;
  uint8_t byte_count;
  uint8_t bytes[LEGACY_MAX_BYTES];
} LegacyReceiveContext;

typedef struct {
  const uint32_t *timings;
  size_t count;
  size_t index;
} LegacyTransmitContext;

static bool is_cancelled(TamaCancelCallback callback, void *context) {
  return callback && callback(context);
}

static bool legacy_decode_signal(InfraredWorkerSignal *signal,
                                 uint8_t output[LEGACY_MAX_BYTES],
                                 uint8_t *byte_count) {
  if (infrared_worker_signal_is_decoded(signal))
    return false;
  const uint32_t *timings;
  size_t count;
  bool checksum_valid;
  infrared_worker_get_raw_signal(signal, &timings, &count);
  return tama_legacy_decode(timings, count, output, byte_count, &checksum_valid) &&
         checksum_valid;
}

static void legacy_signal_received(void *context,
                                   InfraredWorkerSignal *signal) {
  LegacyReceiveContext *receiver = context;
  if (receiver->received)
    return;
  uint8_t byte_count = 0;
  uint8_t bytes[LEGACY_MAX_BYTES];
  if (legacy_decode_signal(signal, bytes, &byte_count)) {
    memcpy(receiver->bytes, bytes, byte_count);
    receiver->byte_count = byte_count;
    receiver->received = true;
  }
}

static TamaIrReceiveResult
legacy_receive(InfraredWorker *worker, uint8_t output[LEGACY_MAX_BYTES], uint8_t *byte_count,
               uint32_t timeout_ms, TamaCancelCallback cancelled,
               void *context) {
  LegacyReceiveContext receiver = {0};
  infrared_worker_rx_set_received_signal_callback(worker,
                                                  legacy_signal_received,
                                                  &receiver);
  infrared_worker_rx_start(worker);
  furi_hal_infrared_async_rx_set_timeout(7500U);

  const uint32_t deadline = furi_get_tick() + furi_ms_to_ticks(timeout_ms);
  while (!receiver.received && furi_get_tick() < deadline &&
         !is_cancelled(cancelled, context)) {
    furi_delay_ms(2);
  }

  infrared_worker_rx_stop(worker);
  if (receiver.received) {
    memcpy(output, receiver.bytes, receiver.byte_count);
    *byte_count = receiver.byte_count;
    return TamaIrReceiveSuccess;
  }
  return is_cancelled(cancelled, context) ? TamaIrReceiveCancelled
                                          : TamaIrReceiveTimeout;
}

static FuriHalInfraredTxGetDataState
legacy_transmit_callback(void *context, uint32_t *duration, bool *level) {
  LegacyTransmitContext *transmitter = context;
  *duration = transmitter->timings[transmitter->index];
  *level = (transmitter->index % 2U) == 0U;
  transmitter->index++;
  return transmitter->index == transmitter->count
             ? FuriHalInfraredTxGetDataStateLastDone
             : FuriHalInfraredTxGetDataStateOk;
}

static void legacy_send_timings(const uint32_t *timings, size_t count) {
  LegacyTransmitContext transmitter = {
      .timings = timings,
      .count = count,
      .index = 0,
  };
  furi_hal_infrared_async_tx_set_data_isr_callback(legacy_transmit_callback,
                                                   &transmitter);
  furi_hal_infrared_async_tx_start(38000U, 0.33f);
  furi_hal_infrared_async_tx_wait_termination();
}

static bool legacy_send(const uint8_t *message, uint8_t byte_count,
                        TamaLegacyPeer sender) {
  if (byte_count != 9U && byte_count != 18U && byte_count != 20U)
    return false;
  const bool v3_timing = sender == TamaLegacyPeerV3;
  const uint16_t bit_count = (uint16_t)byte_count * 8U;
  uint32_t timings[LEGACY_MAX_TIMINGS];
  timings[0] = v3_timing ? 9880U : 9570U;
  timings[1] = v3_timing ? 2480U : 2400U;
  size_t output = 2U;
  for (uint16_t bit = 0; bit < bit_count; bit++) {
    const bool one =
        (message[bit / 8U] & (uint8_t)(1U << (bit % 8U))) != 0U;
    timings[output++] = v3_timing ? 493U : 484U;
    timings[output++] = one ? (v3_timing ? 1370U : 1320U)
                            : (v3_timing ? 750U : 720U);
  }
  timings[output++] = v3_timing ? 1215U : 1208U;
  legacy_send_timings(timings, output);
  return true;
}

static void report_progress(TamaProgressCallback callback,
                            TamaTransferStage stage, uint8_t current,
                            uint8_t total, void *context) {
  if (callback)
    callback(stage, current, total, context);
}

static TamaLegacyActivity legacy_activity_from_type(uint8_t type) {
  switch (type) {
  case 0x04:
    return TamaLegacyActivityTugOfWar;
  case 0x06:
    return TamaLegacyActivityBalloon;
  case 0x08:
    return TamaLegacyActivityQuickEating;
  case 0x0A:
    return TamaLegacyActivityGift;
  default:
    return TamaLegacyActivityUnknown;
  }
}

static bool legacy_send_identity(
    const uint8_t *identity, uint8_t identity_count, TamaLegacyPeer sender,
    TamaProgressCallback progress, void *context,
    TamaLegacySummary *state, TamaLegacySummary *summary) {
  report_progress(progress, TamaTransferStageSendingAck, 35, 100, context);
  /* Preserve the hardware-tested delay before both initial and retry replies. */
  furi_delay_ms(100U);
  state->identity_attempts++;
  if (summary)
    *summary = *state;
  if (!legacy_send(identity, identity_count, sender))
    return false;
  report_progress(progress, TamaTransferStageWaitingRequest, 55, 100, context);
  return true;
}

static TamaTransferResult legacy_exchange(
    InfraredWorker *worker,
    TamaCancelCallback cancelled, TamaProgressCallback progress, void *context,
    TamaLegacySummary *summary) {
  uint8_t initial[LEGACY_MAX_BYTES];
  uint8_t initial_count = 0;
  uint8_t request[LEGACY_MAX_BYTES];
  uint8_t request_count = 0;
  uint8_t identity[LEGACY_MAX_BYTES];
  uint8_t identity_count;
  uint8_t response[9] = {0};
  TamaLegacySummary local_summary = {0};

  if (summary)
    *summary = local_summary;

  report_progress(progress, TamaTransferStageWaitingFirst, 5, 100, context);
  while (true) {
    TamaIrReceiveResult result =
        legacy_receive(worker, initial, &initial_count, 1000U, cancelled, context);
    if (result == TamaIrReceiveCancelled)
      return TamaTransferResultCancelled;
    if (result == TamaIrReceiveSuccess && initial[1] == 0x00 &&
        tama_legacy_peer_from_length(initial_count) != TamaLegacyPeerUnknown) {
      break;
    }
  }

  local_summary.peer =
      tama_legacy_peer_from_length(initial_count);
  local_summary.initial_bytes = initial_count;
  local_summary.last_rx_bytes = initial_count;
  local_summary.last_rx_type = initial[1];
  if (summary)
    *summary = local_summary;

  TamaLegacyPeer sender;
  identity_count = tama_legacy_identity(local_summary.peer, initial, identity, &sender);
  if (!legacy_send_identity(identity, identity_count, sender, progress, context,
                            &local_summary, summary))
    return TamaTransferResultRadioError;

  bool received_request = false;
  for (uint8_t attempt = 0; attempt < 4U; attempt++) {
    TamaIrReceiveResult result =
        legacy_receive(worker, request, &request_count, 1000U, cancelled, context);
    if (result == TamaIrReceiveCancelled)
      return TamaTransferResultCancelled;
    if (result == TamaIrReceiveSuccess) {
      local_summary.last_rx_bytes = request_count;
      local_summary.last_rx_type = request[1];
      if (summary)
        *summary = local_summary;
    }
    if (result == TamaIrReceiveSuccess && request_count == 9U) {
      received_request = true;
      break;
    }
    if (result == TamaIrReceiveSuccess && request[1] == 0x00 &&
        request_count == initial_count) {
      /* The initiator retries its full frame when it missed our identity. */
      memcpy(initial, request, request_count);
      identity_count = tama_legacy_identity(local_summary.peer, initial, identity, &sender);
      local_summary.initial_retries++;
      if (!legacy_send_identity(identity, identity_count, sender, progress, context,
                                &local_summary, summary))
        return TamaTransferResultRadioError;
    }
  }
  if (!received_request)
    return TamaTransferResultTimeout;

  local_summary.request_type = request[1];
  local_summary.activity = legacy_activity_from_type(request[1]);
  if (local_summary.activity == TamaLegacyActivityUnknown)
    return TamaTransferResultInvalidRequest;
  tama_legacy_result(local_summary.peer, initial, identity, request, response);

  report_progress(progress, TamaTransferStageSendingResult, 85, 100, context);
  furi_delay_ms(90U);
  if (!legacy_send(response, sizeof(response), sender))
    return TamaTransferResultRadioError;

  local_summary.response_type = response[1];
  if (summary)
    *summary = local_summary;
  report_progress(progress, TamaTransferStageComplete, 100, 100, context);
  return TamaTransferResultSuccess;
}

TamaTransferResult tama_protocol_legacy_fallback(
    TamaCancelCallback cancelled, TamaProgressCallback progress, void *context,
    TamaLegacySummary *summary) {
  /* One allocation per exchange; RX still stops before every transmission. */
  InfraredWorker *worker = infrared_worker_alloc();
  infrared_worker_rx_enable_signal_decoding(worker, false);
  TamaTransferResult result =
      legacy_exchange(worker, cancelled, progress, context, summary);
  infrared_worker_free(worker);
  return result;
}
