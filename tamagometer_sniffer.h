#pragma once

#include "tamagometer_protocol.h"

#include <stdbool.h>
#include <stdint.h>

#define TAMA_SNIFFER_MAX_BYTES        20U
#define TAMA_SNIFFER_FILE_NAME_LENGTH 40U

typedef struct {
    uint32_t captured_frames;
    uint32_t legacy_frames;
    uint32_t valid_checksums;
    uint32_t dropped_frames;
    uint8_t last_byte_count;
    bool last_checksum_valid;
    bool write_failed;
    uint8_t last_bytes[TAMA_SNIFFER_MAX_BYTES];
    char file_name[TAMA_SNIFFER_FILE_NAME_LENGTH];
} TamaSnifferSummary;

typedef void (*TamaSnifferProgressCallback)(const TamaSnifferSummary* summary, void* context);

bool tama_sniffer_capture(
    TamaCancelCallback cancelled,
    TamaSnifferProgressCallback progress,
    void* context,
    TamaSnifferSummary* summary);
