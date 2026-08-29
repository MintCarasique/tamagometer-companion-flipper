#include "tamagometer_protocol.h"

#include <furi.h>
#include <furi_hal_infrared.h>
#include <furi_hal_rfid.h>
#include <infrared.h>
#include <infrared_transmit.h>
#include <infrared_worker.h>
#include <string.h>

#define MATCH_TIMING(x, v, delta)                                              \
  (((x) < ((v) + (delta))) && ((x) > ((v) - (delta))))
#define FRIENDS_REPEAT_COUNT 10U
#define MESSAGE_BITS 160U
#define LEGACY_MAX_BYTES 20U
#define LEGACY_MAX_TIMINGS 323U

typedef struct {
  uint32_t header_mark;
  uint32_t header_mark_tolerance;
  uint32_t header_space;
  uint32_t header_space_tolerance;
  uint32_t data_mark;
  uint32_t data_mark_tolerance;
  uint32_t data_0_space;
  uint32_t data_0_space_tolerance;
  uint32_t data_1_space;
  uint32_t data_1_space_tolerance;
  uint32_t ending_mark;
} DecoderTimings;

static const DecoderTimings ir = {
    .header_mark = 9600,
    .header_mark_tolerance = 2000,
    .header_space = 5000,
    .header_space_tolerance = 1500,
    .data_mark = 550,
    .data_mark_tolerance = 300,
    .data_0_space = 600,
    .data_0_space_tolerance = 400,
    .data_1_space = 1500,
    .data_1_space_tolerance = 500,
    .ending_mark = 1100,
};

static const char gift_response_2[] =
    "00001110000000011011111100100010001011000000000100001110000000011010000010"
    "10000000000000011001000010001000000000000001000000000000000000000000000000"
    "000011110110";
static const char gift_response_4_template[] =
    "00001110000001111011111100100010001011000000000100001110000000011010000010"
    "10000000000000000000000000000000000000100001000000000000000000000000000000"
    "000011110110";

typedef struct {
  volatile bool received;
  char *output_bits;
} IrReceiveContext;

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

static const uint8_t legacy_v2_identity[] = {
    /* Latest exact V2 response accepted by this V3 in capture 002. */
    0x0C, 0x01, 0x3A, 0x16, 0x62, 0x59, 0x53, 0x67, 0x60,
    0x03, 0x43, 0x04, 0x66, 0x20, 0x30, 0xA2, 0x0B, 0xDF,
};

static const uint8_t legacy_v3_identity[] = {
    0x0C, 0x01, 0x05, 0x56, 0x57, 0x59, 0x60, 0x60, 0x6B, 0x00,
    0x31, 0x84, 0x15, 0x07, 0x20, 0x80, 0x09, 0xFF, 0xFF, 0x00,
};

static bool is_cancelled(TamaCancelCallback callback, void *context) {
  return callback && callback(context);
}

static bool decode_ir(InfraredWorkerSignal *signal, char *bits) {
  const uint32_t *timings;
  size_t count;
  infrared_worker_get_raw_signal(signal, &timings, &count);
  if (count < 323)
    return false;
  if (!MATCH_TIMING(timings[0], ir.header_mark, ir.header_mark_tolerance) ||
      !MATCH_TIMING(timings[1], ir.header_space, ir.header_space_tolerance)) {
    return false;
  }

  size_t timing = 2;
  for (size_t bit = 0; bit < MESSAGE_BITS; bit++, timing += 2) {
    if (!MATCH_TIMING(timings[timing], ir.data_mark, ir.data_mark_tolerance))
      return false;
    if (MATCH_TIMING(timings[timing + 1], ir.data_0_space,
                     ir.data_0_space_tolerance)) {
      bits[bit] = '0';
    } else if (MATCH_TIMING(timings[timing + 1], ir.data_1_space,
                            ir.data_1_space_tolerance)) {
      bits[bit] = '1';
    } else {
      return false;
    }
  }
  bits[MESSAGE_BITS] = '\0';
  return true;
}

static void signal_received(void *context, InfraredWorkerSignal *signal) {
  IrReceiveContext *receiver = context;
  if (!receiver->received && decode_ir(signal, receiver->output_bits))
    receiver->received = true;
}

static bool legacy_timing_between(uint32_t value, uint32_t minimum,
                                  uint32_t maximum) {
  return value >= minimum && value <= maximum;
}

static bool legacy_decode_signal(InfraredWorkerSignal *signal,
                                 uint8_t output[LEGACY_MAX_BYTES],
                                 uint8_t *byte_count) {
  if (infrared_worker_signal_is_decoded(signal))
    return false;

  const uint32_t *timings;
  size_t count;
  infrared_worker_get_raw_signal(signal, &timings, &count);
  if (count < 146U || !legacy_timing_between(timings[0], 7500U, 11500U) ||
      !legacy_timing_between(timings[1], 1700U, 3300U)) {
    return false;
  }

  uint16_t bit_count;
  if (count >= 322U) {
    bit_count = 160U;
  } else if (count >= 290U) {
    bit_count = 144U;
  } else {
    bit_count = 72U;
  }
  if (2U + bit_count * 2U > count)
    return false;

  memset(output, 0, LEGACY_MAX_BYTES);
  for (uint16_t bit = 0; bit < bit_count; bit++) {
    const uint32_t mark = timings[2U + bit * 2U];
    const uint32_t space = timings[3U + bit * 2U];
    if (!legacy_timing_between(mark, 250U, 850U) ||
        !legacy_timing_between(space, 450U, 1900U)) {
      return false;
    }
    if (space >= 1050U)
      output[bit / 8U] |= (uint8_t)(1U << (bit % 8U));
  }

  *byte_count = (uint8_t)(bit_count / 8U);
  if (output[0] != 0x0C)
    return false;
  uint8_t checksum = 0;
  for (uint8_t index = 0; index + 1U < *byte_count; index++)
    checksum = (uint8_t)(checksum + output[index]);
  return checksum == output[*byte_count - 1U];
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
legacy_receive(uint8_t output[LEGACY_MAX_BYTES], uint8_t *byte_count,
               uint32_t timeout_ms, TamaCancelCallback cancelled,
               void *context) {
  LegacyReceiveContext receiver = {0};
  InfraredWorker *worker = infrared_worker_alloc();
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
  infrared_worker_free(worker);
  if (receiver.received) {
    memcpy(output, receiver.bytes, receiver.byte_count);
    *byte_count = receiver.byte_count;
    return TamaIrReceiveSuccess;
  }
  return is_cancelled(cancelled, context) ? TamaIrReceiveCancelled
                                          : TamaIrReceiveTimeout;
}

static void legacy_set_checksum(uint8_t *message, uint8_t byte_count) {
  uint8_t checksum = 0;
  for (uint8_t index = 0; index + 1U < byte_count; index++)
    checksum = (uint8_t)(checksum + message[index]);
  message[byte_count - 1U] = checksum;
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

TamaIrReceiveResult tama_protocol_ir_receive(char output_bits[161],
                                             uint32_t timeout_ms,
                                             TamaCancelCallback cancelled,
                                             void *context) {
  IrReceiveContext receiver = {.received = false, .output_bits = output_bits};
  InfraredWorker *worker = infrared_worker_alloc();
  infrared_worker_rx_set_received_signal_callback(worker, signal_received,
                                                  &receiver);
  infrared_worker_rx_start(worker);
  furi_hal_infrared_async_rx_set_timeout(ir.header_space +
                                         ir.header_space_tolerance);

  const uint32_t deadline = furi_get_tick() + furi_ms_to_ticks(timeout_ms);
  while (!receiver.received && furi_get_tick() < deadline &&
         !is_cancelled(cancelled, context)) {
    furi_delay_ms(2);
  }

  infrared_worker_rx_stop(worker);
  infrared_worker_free(worker);
  if (receiver.received)
    return TamaIrReceiveSuccess;
  return is_cancelled(cancelled, context) ? TamaIrReceiveCancelled
                                          : TamaIrReceiveTimeout;
}

static bool ir_bits_to_timings(const char *bitstring, uint32_t timings[323]) {
  if (strlen(bitstring) != MESSAGE_BITS)
    return false;
  timings[0] = ir.header_mark;
  timings[1] = ir.header_space;
  size_t output = 2;
  for (size_t bit = 0; bit < MESSAGE_BITS; bit++) {
    timings[output++] = ir.data_mark;
    if (bitstring[bit] == '0') {
      timings[output++] = ir.data_0_space;
    } else if (bitstring[bit] == '1') {
      timings[output++] = ir.data_1_space;
    } else {
      return false;
    }
  }
  timings[output] = ir.ending_mark;
  return true;
}

bool tama_protocol_ir_send(const char *bitstring) {
  uint32_t timings[323];
  if (!ir_bits_to_timings(bitstring, timings))
    return false;
  infrared_send_raw(timings, 323, true);
  return true;
}

static uint8_t bits_to_byte(const char *bits) {
  uint8_t value = 0;
  for (uint8_t bit = 0; bit < 8; bit++)
    value = (uint8_t)((value << 1U) | (bits[bit] == '1'));
  return value;
}

static void byte_to_bits(uint8_t value, char *bits) {
  for (uint8_t bit = 0; bit < 8; bit++)
    bits[bit] = (value & (1U << (7U - bit))) ? '1' : '0';
}

static bool message_checksum_valid(const char *message) {
  uint8_t checksum = 0;
  for (uint8_t byte = 0; byte < 19; byte++)
    checksum += bits_to_byte(message + byte * 8U);
  return checksum == bits_to_byte(message + 19U * 8U);
}

static void build_gift_response(uint8_t item_id, const char *request,
                                char output[161]) {
  memcpy(output, gift_response_4_template, sizeof(gift_response_4_template));
  const uint8_t request_type = bits_to_byte(request + 8);
  const uint8_t response_type = request_type == 0x04 ? 0x05 : 0x07;
  byte_to_bits(response_type, output + 8);
  byte_to_bits(item_id, output + 14U * 8U);
  uint8_t checksum = 0;
  for (uint8_t byte = 0; byte < 19; byte++)
    checksum += bits_to_byte(output + byte * 8U);
  byte_to_bits(checksum, output + 19U * 8U);
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

TamaTransferResult tama_protocol_legacy_fallback(
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
        legacy_receive(initial, &initial_count, 1000U, cancelled, context);
    if (result == TamaIrReceiveCancelled)
      return TamaTransferResultCancelled;
    if (result == TamaIrReceiveSuccess && initial[1] == 0x00 &&
        (initial_count == 18U || initial_count == 20U)) {
      break;
    }
  }

  local_summary.peer =
      initial_count == 18U ? TamaLegacyPeerV2 : TamaLegacyPeerV3;
  local_summary.initial_bytes = initial_count;
  local_summary.last_rx_bytes = initial_count;
  local_summary.last_rx_type = initial[1];
  if (summary)
    *summary = local_summary;

  if (local_summary.peer == TamaLegacyPeerV2) {
    memcpy(identity, legacy_v3_identity, sizeof(legacy_v3_identity));
    identity_count = sizeof(legacy_v3_identity);
  } else {
    memcpy(identity, legacy_v2_identity, sizeof(legacy_v2_identity));
    identity_count = sizeof(legacy_v2_identity);
  }
  const TamaLegacyPeer sender = local_summary.peer == TamaLegacyPeerV2
                                    ? TamaLegacyPeerV3
                                    : TamaLegacyPeerV2;
  legacy_set_checksum(identity, identity_count);
  report_progress(progress, TamaTransferStageSendingAck, 35, 100, context);
  /* Observed reply start is about 100-120 ms after the previous frame ends. */
  furi_delay_ms(100U);
  local_summary.identity_attempts++;
  if (summary)
    *summary = local_summary;
  if (!legacy_send(identity, identity_count, sender))
    return TamaTransferResultRadioError;

  report_progress(progress, TamaTransferStageWaitingRequest, 55, 100, context);
  bool received_request = false;
  for (uint8_t attempt = 0; attempt < 4U; attempt++) {
    TamaIrReceiveResult result =
        legacy_receive(request, &request_count, 1000U, cancelled, context);
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
      local_summary.initial_retries++;
      report_progress(progress, TamaTransferStageSendingAck, 35, 100, context);
      furi_delay_ms(100U);
      local_summary.identity_attempts++;
      if (summary)
        *summary = local_summary;
      if (!legacy_send(identity, identity_count, sender))
        return TamaTransferResultRadioError;
      report_progress(progress, TamaTransferStageWaitingRequest, 55, 100,
                      context);
    }
  }
  if (!received_request)
    return TamaTransferResultTimeout;

  local_summary.request_type = request[1];
  local_summary.activity = legacy_activity_from_type(request[1]);
  if (local_summary.activity == TamaLegacyActivityUnknown)
    return TamaTransferResultInvalidRequest;
  local_summary.response_type = (uint8_t)(request[1] + 1U);

  response[0] = 0x0C;
  response[1] = local_summary.response_type;
  response[2] = identity[2];
  response[3] = identity[3];
  response[4] = local_summary.activity == TamaLegacyActivityGift ? 0x00 : 0x01;
  response[5] = local_summary.activity == TamaLegacyActivityGift
                    ? (local_summary.peer == TamaLegacyPeerV2 ? 0x10 : 0x11)
                    : 0x00;
  response[6] = identity[12];
  response[7] = initial[2];
  legacy_set_checksum(response, sizeof(response));

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

TamaTransferResult
tama_protocol_connection_transfer(uint8_t item_id, TamaCancelCallback cancelled,
                                  TamaProgressCallback progress,
                                  void *context) {
  char first[161];
  char request[161];
  char response[161];
  report_progress(progress, TamaTransferStageWaitingFirst, 5, 100, context);

  while (true) {
    TamaIrReceiveResult receive =
        tama_protocol_ir_receive(first, 1000, cancelled, context);
    if (receive == TamaIrReceiveSuccess && message_checksum_valid(first))
      break;
    if (receive == TamaIrReceiveCancelled)
      return TamaTransferResultCancelled;
  }

  report_progress(progress, TamaTransferStageSendingAck, 35, 100, context);
  if (!tama_protocol_ir_send(gift_response_2))
    return TamaTransferResultRadioError;
  report_progress(progress, TamaTransferStageWaitingRequest, 50, 100, context);

  bool received = false;
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    TamaIrReceiveResult result =
        tama_protocol_ir_receive(request, 1000, cancelled, context);
    if (result == TamaIrReceiveCancelled)
      return TamaTransferResultCancelled;
    if (result == TamaIrReceiveSuccess && message_checksum_valid(request)) {
      received = true;
      break;
    }
  }
  if (!received)
    return TamaTransferResultTimeout;

  const uint8_t request_type = bits_to_byte(request + 8);
  if (request_type != 0x04 && request_type != 0x06)
    return TamaTransferResultInvalidRequest;
  build_gift_response(item_id, request, response);
  report_progress(progress, TamaTransferStageSendingGift, 85, 100, context);
  if (!tama_protocol_ir_send(response) || !tama_protocol_ir_send(response)) {
    return TamaTransferResultRadioError;
  }
  report_progress(progress, TamaTransferStageComplete, 100, 100, context);
  return TamaTransferResultSuccess;
}

static void friends_send_byte(uint8_t value) {
  furi_hal_rfid_tim_read_continue();
  furi_delay_us(540);
  furi_hal_rfid_tim_read_pause();
  for (int8_t bit = 7; bit >= 0; bit--) {
    furi_hal_rfid_tim_read_pause();
    furi_delay_us((value & (1U << bit)) ? 650 : 270);
    furi_hal_rfid_tim_read_continue();
    furi_delay_us(150);
  }
  furi_hal_rfid_tim_read_pause();
  furi_delay_us(210);
}

static void friends_send_packet(const uint8_t *packet, size_t length) {
  for (size_t i = 0; i < length; i++)
    friends_send_byte(packet[i]);
}

TamaTransferResult tama_protocol_friends_transfer(uint8_t outcome,
                                                  TamaCancelCallback cancelled,
                                                  TamaProgressCallback progress,
                                                  void *context) {
  static const uint8_t connect_ack[] = {
      0xF0, 0x01, 0x0F, 0x01, 0x01, 0x0F, 0x0B, 0x00, 0x06, 0x80,
      0x02, 0x08, 0x01, 0x08, 0x1A, 0x1A, 0x1A, 0x1A, 0x2D,
  };
  uint8_t reward[] = {0xF0, 0x07, 0x05, 0x01, 0x07, 0x0F, 0x0B, outcome, 0x00};
  reward[8] = (uint8_t)(0x2E + outcome);

  furi_hal_rfid_tim_read_start(134800.0f, 0.5f);
  furi_hal_rfid_pin_pull_release();
  TamaTransferResult result = TamaTransferResultSuccess;
  for (uint8_t repeat = 0; repeat < FRIENDS_REPEAT_COUNT; repeat++) {
    if (is_cancelled(cancelled, context)) {
      result = TamaTransferResultCancelled;
      break;
    }
    friends_send_packet(connect_ack, sizeof(connect_ack));
    if (is_cancelled(cancelled, context)) {
      result = TamaTransferResultCancelled;
      break;
    }
    furi_delay_ms(100);
    friends_send_packet(reward, sizeof(reward));
    report_progress(progress, TamaTransferStageBroadcasting, repeat + 1U,
                    FRIENDS_REPEAT_COUNT, context);
    if (repeat + 1U != FRIENDS_REPEAT_COUNT)
      furi_delay_ms(1000);
  }
  furi_hal_rfid_tim_read_stop();
  furi_hal_rfid_pins_reset();
  if (result == TamaTransferResultSuccess) {
    report_progress(progress, TamaTransferStageComplete, FRIENDS_REPEAT_COUNT,
                    FRIENDS_REPEAT_COUNT, context);
  }
  return result;
}

const char *tama_protocol_result_text(TamaTransferResult result) {
  switch (result) {
  case TamaTransferResultSuccess:
    return "Transfer complete";
  case TamaTransferResultCancelled:
    return "Transfer cancelled";
  case TamaTransferResultTimeout:
    return "Request timed out";
  case TamaTransferResultInvalidRequest:
    return "Unsupported request packet";
  case TamaTransferResultRadioError:
    return "Radio transmission failed";
  default:
    return "Unknown transfer error";
  }
}

const char *tama_protocol_legacy_activity_text(TamaLegacyActivity activity) {
  switch (activity) {
  case TamaLegacyActivityTugOfWar:
    return "Tug-of-war";
  case TamaLegacyActivityBalloon:
    return "Balloon game";
  case TamaLegacyActivityQuickEating:
    return "Quick-eating game";
  case TamaLegacyActivityGift:
    return "Random gift";
  default:
    return "Unknown activity";
  }
}
