#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  TamaTransferResultSuccess,
  TamaTransferResultCancelled,
  TamaTransferResultTimeout,
  TamaTransferResultInvalidRequest,
  TamaTransferResultRadioError,
} TamaTransferResult;

typedef enum {
  TamaTransferStagePreparing,
  TamaTransferStageWaitingFirst,
  TamaTransferStageSendingAck,
  TamaTransferStageWaitingRequest,
  TamaTransferStageSendingGift,
  TamaTransferStageBroadcasting,
  TamaTransferStageComplete,
} TamaTransferStage;

typedef bool (*TamaCancelCallback)(void *context);
typedef void (*TamaProgressCallback)(TamaTransferStage stage, uint8_t current,
                                     uint8_t total, void *context);

typedef enum {
  TamaIrReceiveSuccess,
  TamaIrReceiveTimeout,
  TamaIrReceiveCancelled,
} TamaIrReceiveResult;

TamaIrReceiveResult tama_protocol_ir_receive(char output_bits[161],
                                             uint32_t timeout_ms,
                                             TamaCancelCallback cancelled,
                                             void *context);
bool tama_protocol_ir_send(const char *bitstring);
TamaTransferResult
tama_protocol_connection_transfer(uint8_t item_id, TamaCancelCallback cancelled,
                                  TamaProgressCallback progress, void *context);
TamaTransferResult tama_protocol_friends_transfer(uint8_t outcome,
                                                  TamaCancelCallback cancelled,
                                                  TamaProgressCallback progress,
                                                  void *context);
const char *tama_protocol_result_text(TamaTransferResult result);
