#include "tamagometer_protocol.h"

#include <furi.h>
#include <furi_hal_rfid.h>
#include <infrared.h>
#include <infrared_transmit.h>
#include <infrared_worker.h>
#include <string.h>

#define MATCH_TIMING(x, v, delta)                                              \
  (((x) < ((v) + (delta))) && ((x) > ((v) - (delta))))
#define FRIENDS_REPEAT_COUNT 10U
#define MESSAGE_BITS 160U

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
    return "Gift request timed out";
  case TamaTransferResultInvalidRequest:
    return "Unsupported request packet";
  case TamaTransferResultRadioError:
    return "Radio transmission failed";
  default:
    return "Unknown transfer error";
  }
}
