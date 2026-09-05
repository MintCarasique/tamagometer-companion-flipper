# Tamagometer Enhanced

Tamagometer Enhanced is a hybrid Flipper Zero application. Gifts can be sent
entirely from its on-device interface, and it also connects Flipper Zero to the
[Tamagometer Enhanced](https://github.com/MintCarasique/tamagometer-enhanced)
Windows application. It supports:

- Tamagotchi Connection 2024 infrared receive and transmit;
- Tamagotchi Friends BFF BUMP responses over LF RFID;
- a versioned CLI capability handshake;
- progress reporting for Friends transmissions.

Current stable Companion version: **3.1.0**. Install it together with the
Desktop build from the same release. Companion-specific changes are recorded
in [`CHANGELOG.md`](CHANGELOG.md).

## Standalone interface

- Connection gifts are grouped into Food, Snacks, Items & Toys, Animations,
  and Souvenirs & Special instead of one 181-item list.
- Friends rewards are grouped into Jewelry and Gotchi Points.
- Favorites, recently sent items, and the last successful transfer are stored
  in the app data directory on the SD card.
- The transfer screen provides animated antenna placement guidance, progress,
  and cancellation with Back.
- Settings include vibration feedback and one-file diagnostic export.
- Item details use monochrome conversions of 171 original Tamagometer sprites.
- Connection Sniffer passively records complete original V1/V2/V3 infrared
  sessions without transmitting. Captures contain timestamps, decoded legacy
  bytes, checksum results, and raw timings.
- Original fallback answers a Connection V2 in `Version 1` mode or a
  Connection V3 in `Others` mode either standalone or through the Desktop
  application. The random game/gift exchange is verified on both original
  devices, while fields without sufficient captures remain experimental.

### Original V2/V3 fallback

1. Open **Original fallback** on the Flipper.
2. On a V2 choose **Version 1**, or on a V3 choose **Others**, and start the
   connection.
3. Point the Tamagotchi IR window at the Flipper and keep both devices still.

The Tamagotchi chooses whether the connection becomes a game or a gift. In the
current implementation the Flipper reports the captured responder-win outcome
for games; the Tamagotchi therefore loses. For gifts, the receiving Tamagotchi
chooses the visible result from its own state. The Flipper uses fixed V2/V3 peer
profiles derived from hardware captures, so testing can update the
Tamagotchi's friend and relationship data.

Legacy replies use the low-level infrared HAL because the normal Flipper raw
remote helper adds 180 ms of leading silence. That delay misses the original
Connection response window even when the packet bytes and raw timings are
otherwise correct.

Sniffer files are saved in the app data directory as
`connection_capture_000.txt`, `connection_capture_001.txt`, and so on. Retrieve
them with qFlipper from
`SD Card/apps_data/tamagometer_enhanced/` after stopping the capture.

The app continues to register the `tamagometer` USB CLI command. Keep it open
while using the Desktop application, and do not run another Companion that
registers the same command.

## Install

Download `tamagometer_enhanced.fap` from the
[latest enhanced release](https://github.com/MintCarasique/tamagometer-enhanced/releases/latest)
and copy it to `SD Card/apps/Tools` on the Flipper. The Desktop and Companion
versions should come from the same release.

## Build

Install [uFBT](https://github.com/flipperdevices/flipperzero-ufbt), then run:

```sh
ufbt
```

The generated application is written to `dist/tamagometer_enhanced.fap`.

The item icons can be regenerated from a checkout of the enhanced parent
repository with `tools/convert_item_sprites.py`.

## CLI contract

- `tamagometer info` reports the Companion version, protocol version, and
  capabilities;
- `tamagometer listen` receives a Connection IR frame;
- `tamagometer send<bits>` transmits a 160-bit Connection IR frame;
- `tamagometer legacy` runs one original V2/V3 compatibility exchange;
- `tamagometer friends<0-255>` broadcasts a Friends BFF outcome.

## Fork relationship and attribution

This enhanced Companion is an independent fork of Zach Resmer's MIT-licensed
[tamagometer-companion-flipper](https://github.com/zacharesmer/tamagometer-companion-flipper),
originally created for the upstream
[Tamagometer](https://github.com/zacharesmer/tamagometer) web application.
The upstream repositories are linked here for attribution and project history;
enhanced downloads and support belong to the Tamagometer Enhanced repository.

Friends LF RFID behavior is based on the published Tamagotchi Friends research
credited in the main repository README.

## Disclaimer

This project is unofficial and is not affiliated with Bandai, Tamagotchi, or
Flipper Devices.
