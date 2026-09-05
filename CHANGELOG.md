# Changelog

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
