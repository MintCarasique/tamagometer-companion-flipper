# Connection V4 Others fallback — Initial Support

Released in 3.2.0 as Initial Support. V4 gift receipt and the refactored V4/V3
build were checked on physical hardware on 2026-09-30. Game result semantics
and exhaustive compatibility are not yet confirmed.
This mode emulates a V3 responder. Start Original fallback first, then initiate
Others on the V4 and align the IR windows. The physical device chooses the
activity; individual inventory gifts cannot be selected.

## Capture evidence

The user supplied raw captures from original V4 and V3 devices. The raw fixture
in `tests/fixtures/v4_captures.json` preserves all ten frames from captures 003
and 004, including one damaged frame. Frame directions follow the 24-byte V4
and 20-byte V3 identity sizes. All complete frames use LSB-first encoding and a
byte-sum checksum modulo 256.

Capture 003, V4 initiates:

| Frame | Device | Bytes | Data |
| --- | --- | --- | --- |
| 1 | V4 | 24 | `0C 00 1B D6 51 5E 59 53 55 00 53 88 00 00 20 00 03 FF FF 00 00 00 00 A9` |
| 2 | V3 | 20 | `0C 01 05 D6 57 59 60 60 6B 00 41 84 00 01 20 80 09 FF FF 30` |
| 3 | V4 | 9 | `0C 06 1B D6 01 00 00 05 09` |
| 4 | V3 | 9 | `0C 07 05 D6 00 00 00 1B 09` |

The existing activity mapping calls 06/07 the balloon game. The user recalls
the initiator losing, but this is not a controlled result-byte experiment.
Estimated gaps before replies are 109, 105, and 95 ms.

Capture 004, V3 initiates: a damaged first response is followed by a retry and
complete 20-byte V3 / 24-byte V4 identity exchange, then 08/09 game packets.
The V4 identity checksum is B1. The winner was not recorded. The sniffer's old
20-byte decode falsely reported invalid checksums for both V4 identities.

## Implementation and limits

- Shared production C decoder accepts only complete 9/18/20/24-byte frames,
  retaining invalid-checksum information for the sniffer while fallback rejects it.
- Fallback explicitly disables the worker's remote-control protocol decoding,
  just like the sniffer, so the legacy decoder receives complete raw frames.
  Earlier 5/100 failures and legacy-20 sniffer output came from accidentally
  distributing a stale 2.0.0 FAP. The actual 3.2 development build received a gift.
  There is no evidence isolating worker decoding as the original failure cause.
- A 24-byte initial identity identifies the V4 peer. Flipper sends the exact
  captured V3 profile from 003, adapting byte 3 to the current session and
  recomputing the checksum. V2/V3 profiles are preserved.
- The V4 game response reproduces 003's request byte 4 = 01 / response = 00
  pattern, extrapolating the complementary result for other game requests.
- V4 gift replies use the existing V3 responder format. One successful gift
  receipt is hardware-confirmed, but no complete V4 gift capture was supplied.
- Byte 3 matched in both captured exchanges; adapting it across sessions is an
  inference; a successful gift validates one exchange, not every session value.
  Profile field meanings are incomplete.
- Native V4-to-V4 exchanges, selecting V4 inventory items, and V4.5 are outside
  this implementation. Success means the reply was transmitted; it does not
  independently confirm the device displayed a successful connection.

## Hardware checks

1. Install the 3.2.0 FAP and start Original fallback before starting V4 Others.
2. Verify progress passes 5 and 55, and the V4 completes the activity without FAIL.
3. Repeat several times; record activity, winner, gift appearance, and peer label.
4. If it fails, export Flipper diagnostics and capture the exchange with the
   updated sniffer (V4 identities should now say legacy-24 / checksum: ok).
5. Recheck the already working V2 Version 1 and V3 Others workflows.

## Code ownership

- `tamagometer_legacy.c`: pure decoding, checksums, identity and result bytes;
  shared by fallback, sniffer, and host capture tests.
- `tamagometer_legacy_transfer.c`: Flipper RX/TX lifecycle and exchange stages.
  One worker is allocated per exchange, stopped before transmitting, and freed
  on every exit. Initial/retry replies share the same 100 ms delay; final replies
  retain the 90 ms delay.
- `tamagometer_version.h`: runtime version, protocol, and capability strings.

On Windows, use `tools/build_flipper.ps1` from the parent repository to build
and copy a checked artifact. It invokes the default uFBT target, which installs
the current FAP into `dist`; the app-specific build target alone does not update
an existing `dist` file.

Automated checks: `python tools/test_legacy.py --cc gcc` compiles the production
C module on the host and checks captured decoding, malformed signals, checksum
handling, exact V4 reply bytes, and preserved V2/V3 identity profiles.
