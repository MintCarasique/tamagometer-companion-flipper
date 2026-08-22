# Tamagometer Enhanced

Tamagometer Enhanced is a hybrid Flipper Zero application. Gifts can be sent
entirely from its on-device interface, and it also connects Flipper Zero to the
[Tamagometer Enhanced](https://github.com/MintCarasique/tamagometer-enhanced)
Windows application. It supports:

- Tamagotchi Connection 2024 infrared receive and transmit;
- Tamagotchi Friends BFF BUMP responses over LF RFID;
- a versioned CLI capability handshake;
- progress reporting for Friends transmissions.

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
