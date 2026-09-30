#include "tamagometer_legacy.h"

#include <string.h>

static bool timing_between(uint32_t value, uint32_t minimum, uint32_t maximum) {
    return value >= minimum && value <= maximum;
}

bool tama_legacy_decode(const uint32_t* timings, size_t count,
                        uint8_t output[TAMA_LEGACY_MAX_BYTES],
                        uint8_t* byte_count, bool* checksum_valid) {
    *byte_count = 0;
    *checksum_valid = false;
    memset(output, 0, TAMA_LEGACY_MAX_BYTES);
    uint8_t length = 0;
    const uint8_t lengths[] = {9, 18, 20, 24};
    for(size_t i = 0; i < sizeof(lengths); i++) {
        const size_t data_timings = 2U + lengths[i] * 16U;
        if(count == data_timings || count == data_timings + 1U) {
            length = lengths[i];
            break;
        }
    }
    if(!length || !timing_between(timings[0], 7500U, 11500U) ||
       !timing_between(timings[1], 1700U, 3300U)) return false;

    for(uint16_t bit = 0; bit < length * 8U; bit++) {
        const uint32_t mark = timings[2U + bit * 2U];
        const uint32_t space = timings[3U + bit * 2U];
        if(!timing_between(mark, 250U, 850U) || !timing_between(space, 450U, 1900U))
            return false;
        if(space >= 1050U) output[bit / 8U] |= (uint8_t)(1U << (bit % 8U));
    }
    if(output[0] != 0x0C) return false;
    uint8_t checksum = 0;
    for(uint8_t i = 0; i + 1U < length; i++) checksum = (uint8_t)(checksum + output[i]);
    *byte_count = length;
    *checksum_valid = checksum == output[length - 1U];
    return true;
}

void tama_legacy_set_checksum(uint8_t* message, uint8_t byte_count) {
    uint8_t checksum = 0;
    for(uint8_t i = 0; i + 1U < byte_count; i++) checksum = (uint8_t)(checksum + message[i]);
    message[byte_count - 1U] = checksum;
}

TamaLegacyPeer tama_legacy_peer_from_length(uint8_t byte_count) {
    switch(byte_count) {
    case 18: return TamaLegacyPeerV2;
    case 20: return TamaLegacyPeerV3;
    case 24: return TamaLegacyPeerV4;
    default: return TamaLegacyPeerUnknown;
    }
}

const char* tama_legacy_peer_text(TamaLegacyPeer peer) {
    switch(peer) {
    case TamaLegacyPeerV2: return "V2";
    case TamaLegacyPeerV3: return "V3";
    case TamaLegacyPeerV4: return "V4";
    default: return "unknown";
    }
}

uint8_t tama_legacy_identity(TamaLegacyPeer peer, const uint8_t* initial,
                             uint8_t output[TAMA_LEGACY_MAX_BYTES],
                             TamaLegacyPeer* sender) {
    static const uint8_t v2[] = {
        0x0C, 0x01, 0x3A, 0x16, 0x62, 0x59, 0x53, 0x67, 0x60,
        0x03, 0x43, 0x04, 0x66, 0x20, 0x30, 0xA2, 0x0B, 0xDF,
    };
    static const uint8_t v3[] = {
        0x0C, 0x01, 0x05, 0x56, 0x57, 0x59, 0x60, 0x60, 0x6B, 0x00,
        0x31, 0x84, 0x15, 0x07, 0x20, 0x80, 0x09, 0xFF, 0xFF, 0x00,
    };
    /* V3 response captured in V4 Others -> V3 Others, capture 003. */
    static const uint8_t v3_for_v4[] = {
        0x0C, 0x01, 0x05, 0xD6, 0x57, 0x59, 0x60, 0x60, 0x6B, 0x00,
        0x41, 0x84, 0x00, 0x01, 0x20, 0x80, 0x09, 0xFF, 0xFF, 0x30,
    };
    uint8_t length;
    if(peer == TamaLegacyPeerV4) {
        memcpy(output, v3_for_v4, sizeof(v3_for_v4));
        length = sizeof(v3_for_v4);
        output[3] = initial[3];
        *sender = TamaLegacyPeerV3;
    } else if(peer == TamaLegacyPeerV2) {
        memcpy(output, v3, sizeof(v3));
        length = sizeof(v3);
        *sender = TamaLegacyPeerV3;
    } else if(peer == TamaLegacyPeerV3) {
        memcpy(output, v2, sizeof(v2));
        length = sizeof(v2);
        *sender = TamaLegacyPeerV2;
    } else {
        *sender = TamaLegacyPeerUnknown;
        return 0;
    }
    tama_legacy_set_checksum(output, length);
    return length;
}

void tama_legacy_result(TamaLegacyPeer peer, const uint8_t* initial,
                        const uint8_t* identity, const uint8_t request[9],
                        uint8_t response[9]) {
    const bool gift = request[1] == 0x0A;
    response[0] = 0x0C;
    response[1] = (uint8_t)(request[1] + 1U);
    response[2] = identity[2];
    response[3] = peer == TamaLegacyPeerV4 ? request[3] : identity[3];
    /* Capture 003 V4 request 06/01 -> V3 response 07/00.
       Other V4 activities and winner semantics still need hardware validation. */
    if(gift) {
        response[4] = 0x00;
    } else if(peer == TamaLegacyPeerV4) {
        response[4] = (uint8_t)(request[4] ^ 1U);
    } else {
        response[4] = 0x01;
    }
    response[5] = gift ? (peer == TamaLegacyPeerV3 ? 0x11 : 0x10) : 0x00;
    response[6] = identity[12];
    response[7] = initial[2];
    tama_legacy_set_checksum(response, 9);
}
