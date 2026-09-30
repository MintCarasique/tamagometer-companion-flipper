# Changelog

## [3.2.1] - 2026-09-30

### Changed

- Replaced generic transfer-screen boxes with small Tamagotchi and Flipper
  silhouettes showing screens, buttons, IR windows, and rear LF placement.
- Kept device artwork inside the existing guide area without changing progress,
  title layout, or radio behavior.

### Fixed

- Transfer result text no longer overlaps when an original Connection
  exchange is cancelled or fails. Display a short result and peer separately
  from diagnostic counters, with scrolling for long names and status lines.

## [3.2.0] - 2026-09-30

### Added

- **Initial Support for V4** `Others` fallback using a captured V3 responder profile.
- V4 peer labels in CLI results, on-device status, and diagnostic reports.
- Host C regression tests using raw V4/V3 captures in both directions.

### Fixed

- Shared complete-frame decoding for 9, 18, 20, and 24 bytes; the sniffer now
  reports V4 identities as `legacy-24` with correct checksum results.
- Reject malformed lengths instead of silently truncating them to 20 bytes.

### Compatibility

- Preserves V2/V3 profiles. V4 gift receipt and the refactored V4/V3 build
  were checked on physical hardware. V4 remains Initial Support: game winner
  semantics and exhaustive compatibility are not established. Native V4 mode
  and selectable V4 gifts are not implemented.

### Changed

- Isolated original Connection transport/exchange code from other protocols.
- Reuse one IR worker per exchange and share acknowledgement/retry logic;
  packet bytes and tested response delays are unchanged.
- Centralized runtime version and capability metadata for CLI, About, and
  diagnostics.

## [3.1.0] - 2026-09-05

### Changed

- Simplified standalone worker shutdown so transfer and sniffer threads share
  one lifecycle path.
- Split transfer completion rendering into focused status and result helpers.
- Made SceneManager callback tables use explicit scene indices, reducing the
  risk of a callback being paired with the wrong scene after future changes.
- Simplified item-detail formatting while preserving the existing Connection,
  Friends, and original V2/V3 behavior.

### Verified

- Built successfully against Flipper application API 87.1 for target 7.
- Passed the shared Desktop catalog and protocol-constant consistency tests.

## [2.0.0] - 2026-08-29

### Added

- Standalone SceneManager interface with categorized Connection and Friends
  catalogs, favorites, recent items, repeat-last transfer, item artwork,
  placement guidance, progress, cancellation, diagnostics, and vibration.
- Passive original Connection V1/V2/V3 IR sniffer with decoded packets,
  checksums, and raw timings.
- Hardware-verified original V2 `Version 1` / V3 `Others` compatibility
  fallback for standalone and Desktop-driven use.

### Changed

- The Desktop CLI remains available and now shares the same protocol
  implementation as standalone transfers.

### Fixed

- Corrected transfer-screen layout for the Flipper Zero's 128×64 display.
- Added a low-level timing-safe legacy transmitter that avoids the normal raw
  helper's leading delay and meets original Connection response timing.

## [1.2.0] - 2026-08-22

### Changed

- Published the Desktop UI companion release while keeping the versioned CLI
  compatible with Desktop 1.1.
- Adopted the conventional `tamagometer_enhanced.fap` filename.

## [1.1.0] - 2026-08-22

### Added

- Versioned `info` capability handshake and Friends transmission progress.

### Changed

- Clarified the enhanced fork's relationship to the original project.

## [1.0.0] - 2026-08-22

### Added

- Tamagotchi Friends BFF BUMP support while retaining Connection 2024 IR
  support.

## [0.3]

### Changed

- Added compatibility with Flipper firmware 1.3.0-rc f7 after its
  [CLI API changes](https://github.com/flipperdevices/flipperzero-firmware/pull/4175).

## [0.2]

### Added

- Application images.

## [0.1]

- Initial version.
