#include "tamagometer_legacy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_identity_and_result(void) {
    const uint8_t initial[24] = {
        0x0C, 0x00, 0x1B, 0xD6, 0x51, 0x5E, 0x59, 0x53, 0x55, 0x00,
        0x53, 0x88, 0x00, 0x00, 0x20, 0x00, 0x03, 0xFF, 0xFF, 0x00,
        0x00, 0x00, 0x00, 0xA9,
    };
    const uint8_t expected_identity[20] = {
        0x0C, 0x01, 0x05, 0xD6, 0x57, 0x59, 0x60, 0x60, 0x6B, 0x00,
        0x41, 0x84, 0x00, 0x01, 0x20, 0x80, 0x09, 0xFF, 0xFF, 0x30,
    };
    const uint8_t request[9] = {0x0C, 0x06, 0x1B, 0xD6, 0x01, 0x00, 0x00, 0x05, 0x09};
    const uint8_t expected_result[9] = {0x0C, 0x07, 0x05, 0xD6, 0x00, 0x00, 0x00, 0x1B, 0x09};
    uint8_t identity[24], response[9];
    TamaLegacyPeer sender;
    assert(tama_legacy_identity(TamaLegacyPeerV4, initial, identity, &sender) == 20);
    assert(sender == TamaLegacyPeerV3);
    assert(memcmp(identity, expected_identity, 20) == 0);
    tama_legacy_result(TamaLegacyPeerV4, initial, identity, request, response);
    assert(memcmp(response, expected_result, 9) == 0);

    /* Session bytes must not be frozen to the fixture's D6. */
    uint8_t changed[24];
    memcpy(changed, initial, 24);
    changed[3] = 0x42;
    assert(tama_legacy_identity(TamaLegacyPeerV4, changed, identity, &sender) == 20);
    assert(identity[3] == 0x42 && identity[19] == 0x9C);

    const uint8_t old_v2[18] = {
        0x0C, 0x01, 0x3A, 0x16, 0x62, 0x59, 0x53, 0x67, 0x60,
        0x03, 0x43, 0x04, 0x66, 0x20, 0x30, 0xA2, 0x0B, 0xDF,
    };
    const uint8_t old_v3[20] = {
        0x0C, 0x01, 0x05, 0x56, 0x57, 0x59, 0x60, 0x60, 0x6B, 0x00,
        0x31, 0x84, 0x15, 0x07, 0x20, 0x80, 0x09, 0xFF, 0xFF, 0xBB,
    };
    assert(tama_legacy_identity(TamaLegacyPeerV3, initial, identity, &sender) == 18);
    assert(sender == TamaLegacyPeerV2 && memcmp(identity, old_v2, 18) == 0);
    assert(tama_legacy_identity(TamaLegacyPeerV2, initial, identity, &sender) == 20);
    assert(sender == TamaLegacyPeerV3 && memcmp(identity, old_v3, 20) == 0);
}

static void test_frame_sizes(void) {
    const uint8_t lengths[] = {9, 18, 20, 24};
    for(size_t i = 0; i < sizeof(lengths); i++) {
        uint8_t bytes[24] = {0x0C}, decoded[24], length;
        bool valid;
        const uint8_t size = lengths[i];
        tama_legacy_set_checksum(bytes, size);
        uint32_t timings[387] = {9600, 2400};
        for(unsigned bit = 0; bit < size * 8U; bit++) {
            timings[2 + bit * 2] = 484;
            timings[3 + bit * 2] = (bytes[bit / 8] & (1U << (bit % 8))) ? 1334 : 729;
        }
        timings[2 + size * 16] = 1200;
        for(size_t trailer = 0; trailer < 2; trailer++) {
            assert(tama_legacy_decode(timings, 2 + size * 16 + trailer, decoded, &length, &valid));
            assert(valid && length == size && memcmp(bytes, decoded, size) == 0);
        }
        timings[19] = 1334; /* Correct timing, bad checksum. */
        assert(tama_legacy_decode(timings, 3 + size * 16, decoded, &length, &valid));
        assert(!valid);
        assert(!tama_legacy_decode(timings, 1 + size * 16, decoded, &length, &valid));
    }
    assert(tama_legacy_peer_from_length(18) == TamaLegacyPeerV2);
    assert(tama_legacy_peer_from_length(20) == TamaLegacyPeerV3);
    assert(tama_legacy_peer_from_length(24) == TamaLegacyPeerV4);
    assert(tama_legacy_peer_from_length(9) == TamaLegacyPeerUnknown);
}

int main(void) {
    test_identity_and_result();
    test_frame_sizes();
    unsigned expected_length;
    size_t count;
    unsigned frames = 0;
    while(scanf("%u %zu", &expected_length, &count) == 2) {
        assert(count <= 1024 && expected_length <= 24);
        uint8_t expected[24] = {0}, output[24], length;
        uint32_t timings[1024];
        for(unsigned i = 0; i < expected_length; i++) {
            unsigned value;
            assert(scanf("%u", &value) == 1);
            expected[i] = (uint8_t)value;
        }
        for(size_t i = 0; i < count; i++) {
            unsigned value;
            assert(scanf("%u", &value) == 1);
            timings[i] = value;
        }
        bool valid;
        bool recognized = tama_legacy_decode(timings, count, output, &length, &valid);
        assert(recognized == (expected_length != 0));
        if(recognized) assert(valid && length == expected_length && memcmp(output, expected, length) == 0);
        frames++;
    }
    assert(frames > 0);
    printf("LEGACY_TESTS_OK captured_frames=%u\n", frames);
    return 0;
}
