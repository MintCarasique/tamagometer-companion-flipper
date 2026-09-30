#pragma once

#include "tamagometer_protocol.h"

#define TAMA_LEGACY_MAX_BYTES 24U

/* Decode only complete known frame sizes; report checksum separately for captures. */
bool tama_legacy_decode(const uint32_t* timings, size_t count,
                        uint8_t output[TAMA_LEGACY_MAX_BYTES],
                        uint8_t* byte_count, bool* checksum_valid);

void tama_legacy_set_checksum(uint8_t* message, uint8_t byte_count);
TamaLegacyPeer tama_legacy_peer_from_length(uint8_t byte_count);
const char* tama_legacy_peer_text(TamaLegacyPeer peer);
uint8_t tama_legacy_identity(TamaLegacyPeer peer, const uint8_t* initial,
                             uint8_t output[TAMA_LEGACY_MAX_BYTES],
                             TamaLegacyPeer* sender);
void tama_legacy_result(TamaLegacyPeer peer, const uint8_t* initial,
                        const uint8_t* identity, const uint8_t request[9],
                        uint8_t response[9]);
