# DJI Osmo Action 5 Pro: BLE pairing, Wi-Fi credentials, UDP datalink and live view (spec extracted from source)

> Extracted from open-source code on 2026-09-24 (see section 0). Living reference for this
> project: update it as our own captures confirm or contradict entries marked [UNVERIFIED].

Extracted 2026-09-24 from four open-source code bases. Every claim cites `file:line` in the clones
listed in section 0. Evidence tags:

| tag | meaning |
|---|---|
| **[V-A5P]** | Source author says it was verified on a genuine DJI Osmo Action 5 Pro |
| **[V-XTRA]** | Verified on the Xtra Edge Pro (a rebadged Action 5 Pro on DJI firmware; osmosis `docs/01-protocol-map.md:26-28`) |
| **[V-P3]** | Verified only on an Osmo Pocket 3 |
| **[V-OTHER]** | Verified on another body (Nano, Mavic 3, and so on) |
| **[CODE]** | What the code does. No hardware claim is attached to it. |
| **[UNVERIFIED]** | Inference or open question. Do not rely on it. |
| **[CHECKED]** | Recomputed against frames captured from DJI Mimo (see `tools/test_duml.py`) |

---

## 0. Source repositories

Upstream repositories (cite paths are relative to each repo root):

| repo | url | commit studied |
|---|---|---|
| KonradIT/osmosis | github.com/KonradIT/osmosis | `6992036a` (2026-09-17) |
| eerimoq/moblin (sparse: DJI files only) | github.com/eerimoq/moblin | `2a908f10` (2026-09-24) |
| dji-sdk/Osmo-GPS-Controller-Demo | github.com/dji-sdk/Osmo-GPS-Controller-Demo | `92fe23e5` (2025-11-17) |
| showx/PocketShow (pre-existing) | github.com/showx/PocketShow | `49a12bb4` (2026-09-21) |

Key files:
- osmosis: `app/src/main/java/dev/konraditurbe/osmosis/{ble/GattClient.kt, ble/BleConstants.kt, ble/BleAdvert.kt, ble/CameraModel.kt, duml/DjiCrc.kt, duml/DjiMessage.kt, duml/OsmoCommands.kt, duml/Payloads.kt, net/DumlTransport.kt, net/DumlSession.kt, camera/CameraSession.kt, ui/MainActivity.kt}`, plus `docs/01-protocol-map.md` and `MEDIA_PROTOCOL.md`
- moblin: `Moblin/Integrations/Dji/DjiMessage.swift`, `Moblin/Integrations/Dji/DjiDevice/{DjiDevice,DjiDeviceMessage,DjiDeviceModel,DjiDeviceScanner}.swift`, `Moblin/Various/Settings/SettingsDjiDevice.swift`
- GPS demo: `docs/protocol.md`, `docs/protocol_data_segment.md`, `docs/Q&A.md`, `ble/ble.c`, `logic/connect_logic.c`, `utils/crc/custom_crc{16,32}.{c,h}`
- PocketShow: `src/pocketshow/pocket3/{ble,udp,duml,video,wifi_join}.py`, `src/pocketshow/app.py`

---

## TL;DR

1. **Pairing needs a tap on the camera the first time.** SetPairingPIN (`0x07/0x45`) returns `00 02`. The camera then shows a prompt with your token. The user approves it, and the camera sends a `0x07/0x46` **request** that you must ACK. After that, the same *identifier* string gets `00 01` and no prompt. [V-XTRA] for the first-time path (`docs/01-protocol-map.md:113-115`). [V-A5P] for the full flow (`CameraModel.kt:54-56`).
2. **Wi-Fi credentials come over BLE.** Send `0x07/0x07` (SSID) and `0x07/0x0e` (passphrase), about 500 ms apart. Each reply is `[status][len][ascii]`. [V-XTRA] (`docs/01-protocol-map.md:131-134`).
3. **AP:** `192.168.2.1`, WPA2-PSK, 5.8 GHz, DHCP `192.168.2.x`. On osmosis the AP wake comes from the BLE session sequence (`0x00/0x2b` then `0x53/0x10`), not from `0x07/0x47`.
4. **Datalink port.** A genuine Action 5 Pro uses UDP **9004** plus a TCP-7001 "poke" [V-A5P, tester] (`CameraModel.kt:54-56`, `MEDIA_PROTOCOL.md:28,69`). The Xtra rebadge uses UDP **10004** with no poke [V-XTRA]. Some osmosis prose still says "Action 5 Pro = 10004". Those lines describe the Xtra (see 3.1).
5. **No open-source code shows live view over the AP for an Action 5 Pro.** osmosis only does media offload. PocketShow's live-view start (`0x00/0x88`, `0x00/0x81`, `0x00/0x82`, then `0x00/0x4F` every 200 ms, video in UDP type `0x02` behind a 12-byte sub-header) exists only for the Pocket 3 [V-P3, claimed by PocketShow]. It has **no counterpart in osmosis**. On an Action 5 Pro it is **[UNVERIFIED]**.
6. **A verified video path does exist for the Action 5 Pro: RTMP push (Moblin).** Over BLE only, you tell the camera to join *your* Wi-Fi as a client and push RTMP to a URL. This is the most proven way to get Action 5 Pro video into OBS: run a local RTMP server and add it as a Media Source. See section 4.
7. **[2026-09-26] The low-latency live view also works with the camera on your Wi-Fi**, in 1080p, in Live Streaming mode and without any RTMP stream. See 3.13.

---

## (a) BLE

### a.1 Discovery / identification

| item | value | source |
|---|---|---|
| Manufacturer company ID | DJI `0x08AA` (wire `AA 08`). Xtra `0xF7AA` (wire `AA F7`). A legacy `0xE5C0` is also accepted. | osmosis `BleConstants.kt:11-22`; moblin `DjiDeviceModel.swift:3-4,40-43` |
| Model byte (classic format) | first byte of mfr payload after the company ID: **`0x15` = Action 5 Pro** (LE u16 `15 00`) | osmosis `BleConstants.kt:33`, `BleAdvert.kt:101-105`; moblin `DjiDeviceModel.swift:8,18` (raw bytes `[2..3]` include the company ID) |
| New-format advert | if `payload[5] & 0x04`, the model is a u16-LE *product type* at `payload[10..11]`. Action 5 Pro = **235 (AC204)** [UNVERIFIED on hardware; only 218 = Pocket 4 Pro has been seen] | osmosis `BleAdvert.kt:12-15,26-29,38-42,53` |
| BLE local name | `OsmoAction5Pro-XXXX` (Xtra: `XtraEdgePro-XXXX`) | osmosis `MEDIA_PROTOCOL.md:69-70`, `docs/01-protocol-map.md:22` |
| Xtra detection | MAC OUI `EC:9E:EA` (same model byte `0x15`) | osmosis `Brand.kt:12,24`, `CameraModel.kt:112-129` |
| Official R-SDK filter | raw mfr bytes `[0]=0xAA, [1]=0x08, [4]=0xFA` means "supported camera". What `0xFA` means is **[UNVERIFIED]**: the Nano's payload has `00` there, and the Pocket 3 cycles `00/40/80/c0` (`CameraModel.kt:66-68`). It may be a remote-pairing-mode flag. | GPS demo `ble/ble.c:538-560`, `docs/Q&A.md:47-53` |
| Scan mode | osmosis scans with **no** hardware filter and classifies in software | osmosis `OsmoScanner.kt:11-16,54` |

Recommendation: match on the company ID plus the model byte, and fall back to the name. Do not require `0xFA`.

Model bytes (Moblin `DjiDeviceModel.swift`, eerimoq/moblin `1bb4902`; `0x15` also seen on our
camera): `0x10` Osmo Action 2, `0x12` Action 3, `0x14` Action 4, `0x15` Action 5 Pro, `0x17` Osmo
360, `0x18` Action 6, `0x20` Pocket 3, `0x21` Pocket 4. The app names the model from this table
(`core/camera_model`) and warns for anything but the tested `0x15`.

### a.2 GATT

| item | value | source |
|---|---|---|
| Service | `0000fff0-0000-1000-8000-00805f9b34fb` | osmosis `BleConstants.kt:6`; GPS demo `ble.c:81` |
| Notify (camera→app) | `0000fff4-…` (Pocket 3 sends *all* notifications here; osmosis also subscribes `fff5`) | osmosis `BleConstants.kt:7`, `docs/01-protocol-map.md:34-39`; GPS demo `ble.c:82`, `Q&A.md:9-12` |
| Write (app→camera) | `0000fff5-…`, **write-without-response** (props `0x36`) | osmosis `GattClient.kt:64-70`, `MEDIA_PROTOCOL.md:18`; moblin `DjiDevice.swift:460-465`; GPS demo `ble.c:83` |
| Footgun | DUML written to `fff3` is silently dropped | osmosis `docs/01-protocol-map.md:36-37` |
| CCCD | `00002902-…` | osmosis `BleConstants.kt:9` |
| Encryption/bonding | not required (app-level pairing only) | osmosis `MEDIA_PROTOCOL.md:22`, `docs/01-protocol-map.md:83` |
| Mixed traffic | **On the Action 5 Pro, `fff4` carries both `0x55` DUML frames and `0xAA` R-SDK frames.** Demultiplex on the first byte. | GPS demo `docs/Q&A.md:104-106` |

**Mandatory GATT bring-up order** (osmosis `GattClient.kt:17-21,89-178`, `MEDIA_PROTOCOL.md:13-22`):
1. Connect with LE transport.
2. Request MTU. **Conflict:** the code requests **517** (`GattClient.kt:93`), but osmosis's own doc says "MTU **500**. At 517 the camera stops answering every request" (`MEDIA_PROTOCOL.md:20`), and the DJI demo sets local MTU **500** (`ble.c:225`). Use **500**. Without a raised MTU, notifications cap at about 23 B (`docs/01-protocol-map.md:34-35`).
3. Discover services, then enable notifications (write the CCCD) on **both** `fff4` and `fff5`, one after the other.
4. Write the **value** `01 00` to characteristic `fff4` (the value, not its CCCD), **with response**. This "arms pairing" (`GattClient.kt:158-167`). Settle about 200 ms (`MEDIA_PROTOCOL.md:17`). Skip any step and "the camera ATT-acks every write and answers nothing" (`MEDIA_PROTOCOL.md:14-15`). Moblin skips this write: it just enables notify on every characteristic and sends pairing once the `fff4` notify state flips (`DjiDevice.swift:215-226,467-487`). Whether the `01 00` write is strictly needed on the Action 5 Pro is therefore **[UNVERIFIED]**. Doing it is safe.
5. **Pace `fff5` writes** at least about 10 ms apart. 100–500 ms is a safe margin, because back-to-back write-without-response frames get dropped (`MEDIA_PROTOCOL.md:943-944`, `MainActivity.kt:910-913`).

### a.3 DUML frame (SOF `0x55`), used on BLE, TCP 7001 and UDP

```
off  size  field
0    1     SOF = 0x55
1    2     u16-LE: bits[9:0] = total frame length (13 + payload_len, both CRCs included)
                   bits[15:10] = version = 1   -> byte2 == 0x04 when len < 256 (0x05..0x07 for longer)
3    1     CRC8 over bytes[0:3]
4    1     sender   = (id << 5) | type
5    1     receiver = (id << 5) | type
6    2     msg id / seq, u16 (see note)
8    1     cmd flags  (cmd_type << 5) | encrypt
9    1     CmdSet
10   1     CmdId
11   N     payload
11+N 2     CRC16-LE over bytes[0 : 11+N]
```
Sources: osmosis `docs/01-protocol-map.md:48-77`, `DjiMessage.kt:38-53`, `DumlTransport.kt:342-356`; PocketShow `duml.py:48-79`; moblin `DjiMessage.swift:81-94`.

- **BLE encoders write a 1-byte length and a fixed `0x04`** (osmosis `DjiMessage.kt:41-42`, moblin `DjiMessage.swift:84-85`, PocketShow `ble.py:37-41`). As written, those encoders can only emit frames shorter than 256 B. The UDP parser does handle 10-bit lengths (`DumlTransport.kt:342-356`).
- **Flag byte values seen:** `0x40` = request/needs reply, `0xC0` = response/ACK, `0x00` = push/no reply, `0x80` (cmd_type 4) = what the app uses for `0x00/0x81` device-info over UDP (`docs/01-protocol-map.md:61-62`, `CameraSession.kt:211`, PocketShow `udp.py:325-326`).
- **Addresses** `(id<<5)|type`: App `0x02`, Camera `0x01`, Gimbal `0x03`/`0x04`, Battery `0x05`, **WiFi `0x07`**, DM368 `0x08` (`0x28` = id 1, `0x48` = id 2), session endpoint `0xF0` (type 0x10, id 7), endpoint `0x1C` (type 0x1C, id 0), system/RTC `0x28` (`MEDIA_PROTOCOL.md:32-44`, `OsmoCommands.kt:63-72`). The "target" u16 written LE is `sender | receiver<<8`, so `0x0702` = App→WiFi (`OsmoCommands.kt:5`).
- **The `type` u24-LE** used by osmosis/Moblin is `flags | CmdSet<<8 | CmdId<<16`. For example `0x450740` = flags 0x40, set 0x07, id 0x45 (`OsmoCommands.kt:7-9`, `DjiMessage.kt:55-62`).
- **Msg-id endianness:** osmosis prose disagrees with itself. `docs/01-protocol-map.md:60,71-72` says it is big-endian on BLE, while `MEDIA_PROTOCOL.md:11` says little-endian. It does not matter in practice: every implementation writes it LE and the camera **echoes the bytes verbatim** (`DjiMessage.kt:7-11`). Match responses on the raw 2 bytes.
- **PackString** = `[len:u8][utf8 bytes]` (`Payloads.kt:6-13`, moblin `DjiMessage.swift:29-32`). Moblin's RTMP URL uses `[len:u8][0x00][bytes]` (`DjiMessage.swift:34-37`).

**CRC parameters** [CHECKED: the recomputed CRC8 and CRC16 match all 7 captured frames, for example `550d0433020700a04007077472`]:

| CRC | reflected-algorithm form (what the code uses) | Rocksoft/"spec" form | covers |
|---|---|---|---|
| CRC8 | init `0x77`, poly `0x8C`, LSB-first, no xorout | width 8, poly `0x31`, init `0xEE`, refin=refout=true, xorout `0x00` | bytes `[0:3]` |
| CRC16 | init `0x3692`, poly `0x8408`, LSB-first, no xorout | width 16, poly `0x1021`, init `0x496C`, refin=refout=true, xorout `0x0000` | bytes `[0:len-2]`, stored LE |

Sources: osmosis `DjiCrc.kt:6-31` (vendored from dimadesu/dji-remote); moblin `DjiMessage.swift:7-27` (uses the spec form via CrcSwift); PocketShow `duml.py:10-45` (table form); osmosis `tools/duml_pcap.py:33-60`.

### a.4 Official DJI R-SDK frame (SOF `0xAA`), for reference only

The Action 5 Pro also speaks DJI's official accessory protocol on the same characteristics. It has **no Wi-Fi-credential or AP command**. Its descriptor table covers only mode switch `1D04`, version `0000`, record `1D03`, GPS `0017`, connect `0019`, status subscribe/push `1D05/1D02`, and key report `0011` (GPS demo `docs/protocol.md:87-106`), plus power mode `001A` and restart `0016` in `protocol_data_segment.md`.

```
0 SOF=0xAA | 1 u16-LE ver[15:10]/len[9:0] | 3 CmdType | 4 ENC | 5 RES[3] | 8 SEQ u16 | 10 CRC16 over [0:10]
12 DATA = CmdSet, CmdId, payload | n+12 CRC32 over [0:n+12]
```
(`docs/protocol.md:13-25`)

| CRC | params [CHECKED against `docs/protocol_data_segment.md:165`, where CRC16 = `0xEE57` and CRC32 = `0xD0E1FAF4`] |
|---|---|
| CRC16 | reflected poly `0xA001` (that is `0x8005` refin/refout), **init `0x3AA3`**, no xorout (`custom_crc16.h:50-53`, table `custom_crc16.c:27`) |
| CRC32 | reflected poly `0xEDB88320` (that is `0x04C11DB7` refin/refout), **init `0x00003AA3`**, no xorout (`custom_crc32.h:50-53`, table `custom_crc32.c:28`) |

- Connection `0x00/0x19`: the app sends `device_id,u32 | mac_len,u8 | mac[16] | fw_version,u32 (0) | conidx | verify_mode | verify_data,u16 | reserved[4]`. With `verify_mode = 1`, **the camera shows a verification pop-up for the user to accept**. The camera then sends its own `0x00/0x19` with `verify_mode = 2`, and `verify_data = 0` means allowed. The app must answer that request (`protocol_data_segment.md:24-70`, `connect_logic.c:287-409`). Action 5 Pro `device_id` = `0xFF44` (`protocol_data_segment.md:10`).
- Waking from *sleep*: advertise `0A FF 'W' 'K' 'P' <camera MAC reversed ×6>` for about 2 s. This only works if the device connected recently and the camera has been asleep for 30 min or less (`protocol_data_segment.md:327-344`, `ble.c:831-872`). osmosis notes that Mimo never uses the WKP broadcast (`MainActivity.kt:746-751`).
- Camera-mode enum: `0x1A` = Live Streaming, `0x23` = **UVC Live Streaming** (USB webcam) (`protocol_data_segment.md:220`).

### a.5 Minimal ordered sequence: pair, read SSID and password, wake the AP (osmosis, current `main`)

Receivers: `0x07` = WiFi subsystem, `0xF0`/`0x1C` = session endpoints. Every write goes to `fff5`.

| t (ms) | dir | frame | payload | notes / source |
|---|---|---|---|---|
| 0 | → | `0x00/0x2b` flags 0x40, App→**0xF0** | `04 00` | "session open", Mimo writes this first, before pairing (`MainActivity.kt:1819-1828`, `OsmoCommands.kt:83-91`, `MEDIA_PROTOCOL.md:936-950`). Mimo capture: `550f04a202f01bcb40002b04009ab9`. Addressed to `0x01` instead, the camera answers `e0` (`MEDIA_PROTOCOL.md:46-48`). |
| +120 | → | `0x07/0x45` SetPairingPIN, App→0x07, id `0x8092` | `PackString(identifier) + PackString(token)` | `MainActivity.kt:1830-1834`, `OsmoCommands.kt:26-33`, `Payloads.kt:72-77`. Token `"osmo"` for cameras (`CameraModel.kt:30`). Retry at +2500 and +5000 ms if there is no reply, **with the same identifier** (`MainActivity.kt:1838-1850`). |
| ≤ ~250 | ← | `0x07/0x45` flags 0xC0 | `00 01` = **already paired**, `00 02` = **approval required** | status is `payload[1]` (`MainActivity.kt:1883-1900`). Moblin compares the whole payload to `00 01` (`DjiDevice.swift:275-284`). `MEDIA_PROTOCOL.md:21`: "wait for the 0x07/0x45 pairing reply (up to ~250 ms)". |
| user | ← | if `00 02`: camera shows a prompt with your token (e.g. `OSMO`). After the tap: **`0x07/0x46` flags 0x40 (a REQUEST)**, payload `01` | | `docs/01-protocol-map.md:85-95,106-115`, `MEDIA_PROTOCOL.md:958,973-977` |
| immediately | → | `0x07/0x46` flags **0xC0**, sender/receiver swapped, **same msg id** | osmosis code **echoes the request payload** (`01`). The protocol-map doc says `00`. | `MainActivity.kt:1857-1877` vs `docs/01-protocol-map.md:94`. **[UNVERIFIED which payload the camera wants; the echo is what ships.]** |
| paired + 100 | → | `0x53/0x10` flags 0x40, App→**0x1C** | `00 00 00 00` | "the camera answers `01 00 00 00` and wakes" (`OsmoCommands.kt:93-99`, `MainActivity.kt:917`, `MEDIA_PROTOCOL.md:941`). Mimo capture: `55110492021c1dcb40531000000000894a` [CHECKED]. The Pocket 3 answers `e0` but its AP comes up anyway (`MEDIA_PROTOCOL.md:130`). How the Action 5 Pro answers is **[UNVERIFIED]**. |
| paired + 900 | → | `0x07/0x07` GetWifiSsid, id `0x8007` | *(empty)* | `MainActivity.kt:923` |
| | ← | `0x07/0x07` | `[status u8][len u8][ascii]`, e.g. `00 12 "XtraEdgePro-2DCA16"` | `docs/01-protocol-map.md:127`, parser `MainActivity.kt:943-950` |
| paired + 1400 | → | `0x07/0x0e` GetWifiPassword, id `0x800E` | *(empty)* | `MainActivity.kt:924` |
| | ← | `0x07/0x0e` | `[status u8][len u8][ascii passphrase]` | `docs/01-protocol-map.md:128`, `MainActivity.kt:1911-1918` |
| (opt) | → / ← | `0x07/0x0c` GetWifiMac | reply `[status][6-byte MAC]` | `MEDIA_PROTOCOL.md:999-1002` |
| pass + 3000 | | join the AP (WPA2-PSK) | | `MainActivity.kt:983`. AP IP `192.168.2.1` (`docs/01-protocol-map.md:147-149`) |

**Answer every camera request.** Any inbound frame with flags `0x40` must get a `0xC0` reply with the addresses swapped and the same id. Otherwise "the camera drops us (~6s)". osmosis echoes the payload, except for an inbound `0x00/0x81`, which it answers with the 62-byte APP device-info blob `00 'A' 'P' 'P' 00×37 02 00×8 02 08 00×10` (`MainActivity.kt:1854-1866`, `OsmoCommands.kt:102-108`).

**Is a tap on the camera required? YES, the first time**, for any given *identifier*. The camera keys its remembered approval on that string (`docs/01-protocol-map.md:98-105`, `Payloads.kt:26-33`, `MEDIA_PROTOCOL.md:966-968`). The first-time `00 02` flow was confirmed on a factory-reset Xtra/Action 5 Pro (`docs/01-protocol-map.md:113-115`), and the README tells users to "Approve the pairing prompt on the camera screen (will read: `OSMO`)" (`README.md:27`). The official R-SDK path also needs an on-screen accept (a.4).
- The identifier `284ae5b8d76b3375a04a6417ad71bea3` is shipped by osmosis, Moblin, dji-remote **and** PocketShow (`Payloads.kt:38`, moblin `DjiDeviceMessage.swift:3-10`, PocketShow `ble.py:19-27`). A camera that approved any of those apps will return `00 01` silently. The downside is that every install shares one "remembered slot" (`docs/01-protocol-map.md:102-105`). Best practice is a per-install 32-char id (UUID truncated to 32 chars), minted once and persisted, and reused on retries (`Payloads.kt:52-60`).
- The token is cosmetic on cameras: it is shown on screen, and any value pairs (`docs/01-protocol-map.md:106-109`). Moblin and PocketShow use `"mbln"` (`DjiDevice.swift:30`, `ble.py:28`).

**Waking the AP, and what not to do**
- osmosis relies on the session sequence (`0x00/0x2b` followed by `0x53/0x10`) and **does not send `0x07/0x47`** unless no password was obtained (`MainActivity.kt:952-966`). Mimo never sends `0x07/0x47`, and sending it correlated with a sleeping camera dropping BLE with status 19 (`OsmoCommands.kt:59-61`).
- Fallback: `0x07/0x47` ConnectToWiFi with `PackString(ssid) + PackString(pass)` of the camera's **own** AP. The reply is `00 00` = ok, and the AP comes up about 15 s later (`docs/01-protocol-map.md:142-145`, `MEDIA_PROTOCOL.md:980-984`).
- Documented keepalive: `0x00/0x2b` `01 01` to `0xF0` about every 1 s (`MEDIA_PROTOCOL.md:940,946-953`). **osmosis never runs it.** `startKeepalive()` is defined at `MainActivity.kt:259` and has no caller. The verified flow therefore works without a BLE keepalive.
- The BLE link drops about 5–6 s after pairing if idle. This is by design: you are meant to move to Wi-Fi (`docs/01-protocol-map.md:42-44`).
- **A camera that has never been activated (through Mimo and DJI's servers) has no AP at all.** Credential getters get no reply, `0x07/0x41` is accepted but no AP appears, and activation (`0x00/0x32`) is a server-signed challenge-response that cannot be forged [V-OTHER: Nano] (`docs/01-protocol-map.md:151-195`). The camera pushes its state as `0x00/0x32` sub `0x33`, with the state at `payload[20]`: `0` = not activated, `1` = activated, `2` = uninitialized (`MainActivity.kt:1924-1938`).
- Region: `0x07/0x19` GetCountryCode replies `00 FF <cc:2> 00`. `0x07/0x18` SetCountryCode takes `FF <cc:2> 00` (`docs/01-protocol-map.md:161,192-195`).

---

## (b) Wi-Fi / UDP datalink

> Up to 3.12, the measurements were made with the camera on its own access point, reached through
> the ESP32-S3 USB bridge (the app's network link until 0.1.0). Since 0.2.0 the camera joins your
> network instead (3.13); the datalink is the same.

### 3.1 Ports

| body | datalink | TCP-7001 poke | evidence |
|---|---|---|---|
| **Genuine Action 5 Pro** | **UDP 9004** | **yes** | [V-A5P, tester: grid and download] `CameraModel.kt:54-56`, `MEDIA_PROTOCOL.md:28,69` |
| Xtra Edge Pro (A5P rebadge) | UDP **10004** | no (TCP 7001 refused, no 9004) | [V-XTRA] `CameraModel.kt:109-130`, `MEDIA_PROTOCOL.md:29,70,83`, `docs/01-protocol-map.md:237-244` |
| Pocket 3 | UDP 9004 | yes | [V-P3] `MEDIA_PROTOCOL.md:74`. PocketShow does **not** poke (`app.py:118-146`, `udp.py:124-150`). |

- Stale text: `docs/01-protocol-map.md:22,239-241,356`, `CameraSession.kt:17-19` and `tools/duml_pcap.py:10` say "Action 5 Pro → 10004". Those notes were written against the Xtra. The newer model table and `CameraModel` supersede them. osmosis retries the alternate port if the handshake never lands (`CameraModel.kt:32-38`, `MainActivity.kt:1076-1118`).
- Camera AP = `192.168.2.1`. HTTP (lighttpd 1.4.55) on :80 serves `/v2?storage=N&path=…` (`docs/01-protocol-map.md:210-217`).

### 3.2 TCP 7001 poke (before the UDP handshake, 9004 bodies)

Open TCP to `192.168.2.1:7001` (connect timeout 1200 ms). Write **one SetPairingPIN DUML frame** built with `OsmoCommands.setPairingPin("osmo")`, meaning the default identifier and id `0x8092`. Flush, sleep 400 ms, close. Failures are ignored (`DumlSession.kt:97-106`). Why the camera needs this ("arms the datalink") is not documented.

### 3.3 UDP transport header (8 B, all LE)

```
[0:2] u16 = 0x8000 | total_len   (total = 8 + payload; mask 0x3FFF osmosis / 0x7FFF PocketShow)
[2:4] u16 session id
[4:6] u16 seq
[6]   u8  packet type
[7]   u8  XOR of bytes [0:7]
```
osmosis `DumlTransport.kt:304-322`; PocketShow `udp.py:49-63`. Identical.

| type | meaning | source |
|---|---|---|
| `0x00` | handshake (both directions) | `MEDIA_PROTOCOL.md:621-627` |
| `0x01` | camera data/telemetry: DUML pushes, **plus 34-byte window-status frames** | `DumlTransport.kt:64-75,255-262` |
| `0x02` | **video** (PocketShow only; osmosis never handles it) | PocketShow `udp.py:22,207-217` |
| `0x03` | reliable data (drone manifest in osmosis; "ack telemetry" in PocketShow) | `MEDIA_PROTOCOL.md:625`, `udp.py:23,220-223` |
| `0x04` | app→camera window ACK | `DumlTransport.kt:159-194`, `udp.py:246-258` |
| `0x05` | command (routing header + DUML) | `DumlTransport.kt:196-208`, `udp.py:260-293` |

### 3.4 Handshake

Send a type `0x00` packet with UDP seq `0` (osmosis uses its running counter, which starts at 0) and a **40-byte payload**:
```
[base:u16-LE] 00 64 00 64 00 c0 05 14 00 00 64 00 00 01 90 01 c0 05 14 00 00 64 00 14 00 64 00 c0 05 14 00 00 64 00 01 01 04 01 02
```
- `base` = a **random, 8-aligned** starting sequence, chosen fresh on every connect. A fixed base (the old constant `0x87b8`) can leave the peer's session wedged (`DumlTransport.kt:46-62,113`, `DumlSession.kt:72-85`). The tail is byte-identical in PocketShow (`udp.py:33-41,131-137`). The fields reportedly advertise window 100 and MTU 1472 (`c0 05`) (`DumlSession.kt:74-75`).
- Session id: osmosis picks a random value in `0x1000..0xFFFE` and **keeps it** (`DumlTransport.kt:110`). PocketShow picks a random u16 and then **adopts the session id from the camera's reply** (`udp.py:89,143`). Which one the camera requires is **[UNVERIFIED]**. Both work on their target bodies.
- Retry: osmosis re-sends up to 20× and accepts the first reply datagram with `type == 0x00` (350 ms receive window each) (`DumlTransport.kt:124-130`). PocketShow sends once and blocks on recv with a 10 s timeout (`udp.py:124-150`).
- After the reply, osmosis drains 5×400 ms, sending an ACK after each batch. It learns `camera_channel` from bytes `[8:10]` of every received datagram (≥ 10 B, non-zero) and then sets its **own seq = camera_channel + 8** (`DumlSession.kt:115-121`, `DumlTransport.kt:132-138,251-254`). In practice the camera echoes `base`, so seq ends up as `base + 8` (`DumlTransport.kt:57-59`, `DumlSession.kt:123-131`).

### 3.5 Command packets (type 0x05): `[8B hdr][12B routing][DUML]`

| field | osmosis (verified A5P for media) | PocketShow (Pocket 3) |
|---|---|---|
| UDP hdr seq | own seq, then +8 after **every** packet sent (`DumlTransport.kt:196-208,298`) | cmd seq, +8 per command (`udp.py:112-116,290`) |
| routing `[0:2]` "ack" | own seq − 8 (the previous command's seq). A wrong value "silently drops writes" (`DumlTransport.kt:277-337`, `MEDIA_PROTOCOL.md:629-637`) | seq − 32 (`udp.py:285-286`) |
| routing `[2:4]` | own seq | seq |
| routing `[4:8]` | `00 00 00 00` | `00 00 00 00` |
| routing `[8]` | command counter u8 (+1 per DUML) | msg counter u8 (from 1) |
| routing `[9]` | `01` | `01` |
| routing `[10:12]` | `00 00` (camera); `60 00` for drones | `60 00` (`struct <H 0x0060`) |
| DUML msg id | separate counter from `0xA000`, +1 per frame | same as the UDP seq |
| first seq used | `base + 8` (after syncing to the channel) | `base` |

Both work on their own bodies. For an Action 5 Pro, start with the osmosis values, which were verified on A5P for media.

**Write cliff:** a registered session stops accepting *writes* after about 40–70 s (Xtra about 50 s). Reads keep working. osmosis re-registers before any write once the session is older than 40 s (`MEDIA_PROTOCOL.md:642-644`, `DumlSession.kt:44-56`). This matters for a long-running live-view heartbeat [UNVERIFIED impact]. PocketShow re-sends `0x00/0x81` + `0x00/0x82` every 2 s (`udp.py:337-338`), which may be its workaround.

### 3.6 Window ACK (type 0x04), the keepalive that keeps data flowing

The payload is 26 B (a **34-byte** datagram) made of three groups `[u16 start][u16 end][u32 0]` in the order **video, download, control**, plus a trailing `u16 0`. The UDP header seq is **0** (`DumlTransport.kt:159-194`, `udp.py:246-253`).

| group | osmosis (`DumlTransport.kt:183-194`) | PocketShow (`udp.py:246-253`) |
|---|---|---|
| video | `peerCursor` = u16 at `[10:12]` of the camera's 34-byte type-0x01 status datagram, echoed as start = end | seq from the UDP header of the latest in-order **type-0x02** packet received |
| download | `peerDownloadCursor` = `[18:20]` of the same status datagram | seq of the latest type-0x03 packet |
| control | fixed `baseSeq` (copying the send seq broke drone paging) | current cmd seq |
| cadence | once per receive round (~200–400 ms) | every **20 ms** (`udp.py:238-244`) |

"The peer holds off streaming until its window is acknowledged" (`DumlTransport.kt:64-67,160-162`). The camera also sends "an empty-payload transport ACK before the real reply — skip it" (`MEDIA_PROTOCOL.md:639-640`, `DumlTransport.kt:371-387`). The AP drops after about 10 s idle, and "the Action 5 tears down its WiFi AP the instant the datalink goes idle" (`docs/01-protocol-map.md:203-204`, `CameraSession.kt:21-22`).

### 3.7 Session registration (osmosis `openAndRegister`, used on A5P for media)

`CameraSession.kt:207-232`, `MEDIA_PROTOCOL.md:646-705`. Between steps: `recvAll(400)`, then ACK.

| # | DUML | recv | flags | payload |
|---|---|---|---|---|
| 1 | `0x00/0x81` device info | DM368 id2 `0x48` | `0x80` | 62 B: `00 41 50 50` + `00×37` + `02` + `00×8` + `02 08` + `00×10` (`CameraSession.kt:2220-2226`) |
| 2 | `0x00/0x88` register / "APP presence" | DM368 id1 `0x28` | `0x40` | 14 B `17 00 46 23 7c 41 50 50 00 00 00 00 00 02`. Bytes 2–5 vary in Mimo (a counter or timestamp); a fixed value is accepted (`CameraSession.kt:76-100`). The doc's 15-byte variant is `170008237b41505000000000000002` (`MEDIA_PROTOCOL.md:655-658`). |
| 3 | `0x03/0xDA` init | Gimbal `0x03` | `0x40` | `05 ff ff ff ff` |
| 4 | `0x00/0x99` subscribe (×8) | `0x28` | `0x40` | `02 02 00 00 | subId:u32-LE (from 0x69DF) | 00 00 00 | (nameLen+6):u16-LE | nameLen:u16-LE | name | 00 00 00 00` (`CameraSession.kt:2252-2275`) |
| 5 | `0x00/0x6a` set time (optional) | `0x28` | `0x40` | `01 00 | unix:u64-LE | utcOffsetMin:i16-LE | tzLen:u8 | IANA tz` (`CameraSession.kt:2234-2250`) |
| loop | `0x00/0x88` (same 14 B) about every 900 ms, plus ACKs about every 300 ms | `0x28` | `0x40` | (`CameraSession.kt:1042-1046`) |

For **live view, do not** send `0x02/0x0c 01 01 00 01` (enter playback). It is a camera-wide mode that takes the body out of capture/live (`MEDIA_PROTOCOL.md:579-600`, `CameraSession.kt:102-118`). Camera status pushes `0x02/0x80`, `0x02/0xdc` and `0x0d/0x02` arrive unprompted once you are registered (`CameraSession.kt:1031-1036`, `MEDIA_PROTOCOL.md:839-905`). Bit 30 of the `0x02/0x80` flags word = playback (`MEDIA_PROTOCOL.md:913-926`).

### 3.8 Live-view start: PocketShow (Pocket 3 only), and its (absent) osmosis counterpart

PocketShow `udp.py:305-343`, run after `connect()` (handshake) and `start()` (RX thread plus a 20 ms ACK thread). There is no TCP poke and no heartbeat drain:

| # | DUML | recv | flags | payload |
|---|---|---|---|---|
| 1 | `0x00/0x88` | DM368 id1 `0x28` | `0x40` ("PUSH") | `17 00 46 23 73 41 50 50 00 00 00 00 00 02` (14 B; differs from osmosis only in byte 4, `0x73` vs `0x7c`) |
| 2 | `0x00/0x81` | DM368 id2 `0x48` | `0x80` ("WRITE") | 64 B: `00 'A' 'P' 'P'` + `00×60` (osmosis sends 62 B with `02`@41 and `02 08`@50–51) |
| 3 | `0x00/0x82` | `0x48` | `0x80` | `00` |
| loop, 200 ms | `0x00/0x4F` | `0x48` | `0x40` | 9 B `01 00 <ctr:u8> 00 00 ff ff ff ff` (`struct "<BBBBBI"`). `ctr` increments every 2nd tick (400 ms). Steps 2 and 3 are re-sent every 10 ticks (2 s). |

Receive side: a **type `0x02`** datagram means `payload = datagram[8:]`. Drop the first **12 bytes** and the rest is **Annex-B H.264**, which is fed straight to `ffmpeg -f h264` (`udp.py:207-217`, `video.py:29-49`). PocketShow does no reordering or loss handling, and nothing documents the 12-byte sub-header. The config default is 1280×720 (`configs/default.example.yaml:79-89`, README "WiFi 720p").

**Counterparts in osmosis. Searched for** `liveview|live view|livestream|h264|annex|0x4F|DM368|video window|type 0x02`:
- **None for live view.** osmosis has no live-view code. It never handles UDP type `0x02` and never sends `0x00/0x82` or `0x00/0x4F` to a camera.
- It does share steps 1 and 2 (`0x00/0x88` → `0x28` and `0x00/0x81` → `0x48`, flags `0x80`) as generic session registration (3.7). So these are proven to be accepted by an A5P datalink session. That they start video on an A5P is **[UNVERIFIED]**.
- It keeps a **video window** in the ACK, taken from `[10:12]` of the camera's 34-byte status frames, and notes that during media sessions "the video window stays frozen the whole time" (`DumlTransport.kt:64-68,164-168`). This implies the A5P datalink has a video channel that is idle unless something starts it.
- `0x00/0x4F` appears only in the **drone** code: `DronePairing.kt:58,63` (BLE, receiver `0x4F` = type 0x0F id 2, payloads `04 00×8` and `01 00 00 00 00 e8 03 00 00`) and `DroneSession.kt:684`. It has the same 9-byte `01 00 …` shape as PocketShow's heartbeat but a different receiver. It is not used on cameras.

### 3.9 Live-view start on the Action 5 Pro over its AP [VERIFIED 2026-09-24, our own test]

**Confirmed working on a genuine Action 5 Pro** (BLE model byte `0x15`, device-info reports `ac204`)
with `tools/dji_liveview.py`: TCP 7001 poke, UDP 9004 handshake (osmosis layout), settle, register
(`0x00/0x81` + `0x00/0x88`), then the PocketShow trigger (`0x00/0x81` + `0x00/0x82`, `0x00/0x4F`
heartbeat every 200 ms, `0x81`/`0x82` every 2 s, `0x88` every 1 s). Observations:

- Video datagrams (type `0x02`) arrive within ~1 s of the trigger and keep flowing for as long as the
  heartbeat runs (90 s tested; the ~50 s write cliff did not bite with the periodic re-registration).
- After the 12-byte sub-header the payload is **Annex-B H.264, High profile, level 3.2, 1280x720,
  ~30 fps**, IDR + SPS/PPS about once per second, AUD + SEI on every frame. DJI also inserts a
  proprietary unit `00 00 01 FF ...` (NAL header byte `0xFF`) after each AUD; decoders skip it.
- Bitrate ~3.5 Mbit/s (2.8-3.7 per second), matching DJI's 4 Mbit/s "image transmission" figure.
- Sub-header samples: `6870 7070 00000000 09 03 6000`, `6870 7870 00000000 09 83 6000`: looks like
  `[u16 window start][u16 seq][u32 0][u8 frame counter][u8 fragment info, 0x80 = last?][u16 ?]`.
  Not needed for decoding (concatenating the payloads in order works).
- ACK the video window with the newest in-order type-`0x02` seq only (never move it backwards),
  otherwise the camera retransmits and duplicates appear.
- The AP is torn down a few seconds after the datalink goes idle; a paired app re-wakes it over BLE
  (`0x00/0x2B`, `0x07/0x45` -> `00 01`, `0x53/0x10`) without another on-screen approval.
- Measured glass-to-glass latency through the ESP32-S3 USB bridge into ffplay: ~135 ms.
- The preview format follows the recording **aspect ratio** only: 16:9 -> 1280x720, 4:3 -> 960x720.
  Recording resolution (4K / 2.7K / 1080p) and frame rate (30/60/120) leave it at 720
  lines and ~30 fps; only the H.264 level toggles between 3.1 and 3.2. A 1080p preview therefore
  does not come from recording settings (tested 2026-09-24; slow-motion modes not tested).
- A recording-format change restarts the camera's video stream with a new sequence number. The
  video ACK must accept such a jump (and fall back to the status-frame video cursor while video is
  stalled), otherwise the camera's send window fills and the video stops for good.

The original plan, kept for reference:

Combine the A5P-verified transport (osmosis) with the Pocket 3 video trigger (PocketShow):
1. BLE: a.5 through the password. Join the AP.
2. TCP 7001 poke (3.2). Then the UDP 9004 handshake with a random base (3.4). If it fails, try 10004 with no poke.
3. Drain and sync seq (3.4). Run the ACK loop at 20–50 ms, echoing the video/download cursors (3.6).
4. Register: `0x00/0x81` (62 B, flags 0x80, to `0x48`) and `0x00/0x88` (14 B, to `0x28`). Do not enter playback.
5. PocketShow trigger: `0x00/0x81` + `0x00/0x82` (flags 0x80, to `0x48`), then `0x00/0x4F` `01 00 ctr 00 00 ff ff ff ff` every 200 ms. Re-send `0x81`/`0x82` every 2 s. Keep `0x00/0x88` at about 1 Hz.
6. Collect type `0x02`, strip 12 bytes, and probe for Annex-B start codes. The A5P may emit **HEVC** instead of H.264 [UNVERIFIED]. Check the NAL headers.
7. If nothing arrives, capture Mimo's live preview (PCAPdroid, as osmosis does with `tools/duml_pcap.py`) and diff it against this sequence.

---

### 3.10 Lost video on the Action 5 Pro [VERIFIED 2026-09-25, our own tests]

- **The camera never re-sends video.** With the app dropping every 50th video datagram on purpose
  and holding the ACK at each gap for 150 ms: 72 dropped, 0 re-sent, 0 duplicates. Holding the ACK
  only throttles the camera (video rate down ~15%, one second at 2.2 Mbit/s). Acknowledge the newest
  datagram at once.
- **No keyframe on request.** `09/A8 00 04 02 00...` (Mimo's live-view enable / IDR request on the
  A6 and Nano) answers `ee` from `0x41` and `e0` (not supported) from `0x08` and `0x48`;
  `02/B3` (`dji_camera_get_app_request_i_frame`) answers `e0` with an empty payload and with `01`.
  Keyframes stay at a fixed 30-frame (1 s) cadence, so a lost datagram damages the picture until the
  next one. The app can hold the last intact frame meanwhile (Options, off by default: real time
  first).
- **Video sub-header** (12 bytes before the H.264 bytes): `[u16 window][u16 seq][u32 0][u8 frame
  counter][u8: bit 7 = odd fragment, bits 0-6 = fragments in the frame][u8: fragment pair index in
  the low bits][u8 flags]`. Keyframes are 18-24 fragments, P-frames 2-13 (at ~3.8 Mbit/s).
- Loss seen with the ESP32-S3 bridge happened on the radio side (bridge and Windows counters clean);
  larger Wi-Fi RX buffering in the bridge (firmware 0.4.0) cut it by about two thirds.

### 3.11 No video after a short power-off [SOLVED 2026-09-25: hang up Bluetooth after the wake]

Switch the camera off while the app streams and on again within about a minute, and the camera
often (not always: 3 of 4 runs were fine at one point) sent no video to any new connection, for
minutes. Off for more than a minute (a cold start) always cleared it; so did replugging the bridge.
Cause and fix: the last paragraphs of this section (DJI Mimo hangs up Bluetooth; the app now does).

What the stuck camera does:

- Everything but video: handshake, status and DUML traffic, ~95 datagrams/s of type `0x01`
  (~72/s of them 34-byte window-status frames), answers to our requests, its own `00/81` requests.
- Its **video cursor** (`[10:12]` of the status frames) never moves: it does not start the video
  channel. A packet capture on the PC (pktmon) shows no type `0x02` datagram to any port, and the
  bridge drops nothing, so it is not lost on the way.
- The camera's status pushes (`cam_status`, `cam_video_param_v2`, `02/80`...) are identical to a
  healthy camera's; it does not say why.

Ruled out (each tried in the stuck state or across a power cycle, no video):

- Reconnecting the datalink at any interval (1-30 s of silence) or after 20 s of quiet; fresh
  handshake session ids and bases (always used); random `00/88` registration bytes.
- Restarting the app while its Bluetooth link was reconnected at once; releasing Bluetooth for 90 s
  while the datalink kept retrying (the link really drops: the camera advertises again 3.3 s later).
- Bridge side: restarting the ESP32 Wi-Fi driver, a clean rejoin (`rejoin`), `forget` then joining
  after a Bluetooth wake (1 of 2 worked), a soft reboot, a reset without deauthentication (`vanish`).
- Joining the camera's Wi-Fi only after the Bluetooth wake (DJI Mimo's order).
- `02/09` liveview_subscribe (answers `e3`, bad parameters), `00/82` `01`/`00`, `00/0E`, `02/0A`
  (`e0`), TCP 6001 (refused on the A5P; Mimo only tries it as one of its link types).
- `07/15` wifi_restart: answers `00` on a healthy camera and restarts its Wi-Fi (the bridge sees
  `AUTH_LEAVE`); on a stuck camera it gets no reply, drops the Bluetooth link and does not help.
- `00/0B` reboot_device to `0x01`: no reply, no reboot.

What did bring video back, besides a cold start or a replug:

- Ending the app's Bluetooth session and starting new ones: the video came on the second or third
  new session, 40 s to 3 min after the power-on (the CLI after closing the app; the app renewing its
  session each time a connection got no video).
- The pairing token matters: the TCP 7001 poke must carry the token the Bluetooth link paired with;
  with a different one a healthy camera sends no video either (tested with random Bluetooth tokens).

DJI Mimo recovers every time. Its Bluetooth sequence per connection (btsnoop, Mimo 2.12.1): `00/2B`
`04 00` → `0xF0`; `07/45` pairing check with **a new 4-digit token each time**; `00/32`
activate_device `"11" 00 00 00` → `0x88`; `02/8E` GET parameter `0x1C` → `0x08`; `07/39`
get_wifi_mode `ee` (answers `e0`); `00/2B` `01 01` about every second; then `07/07` `19`, `07/0E`
`d1`, `07/0C` (SSID, password, AP MAC). It never sends `53/10` and **never answers the camera's own
requests** (`00/81`, `00/88`, `00/74`), which the camera keeps sending; the link stays up. Mimo's
datalink traffic is not captured (PCAPdroid breaks its UDP link).

**Mimo hangs up Bluetooth once it has the Wi-Fi credentials** (HCI Disconnection Complete, reason
`0x16` "terminated by local host", ~1 s after its `07/0C`, in both captured sessions): its live view
runs with no Bluetooth link at all. The app kept the link open (keepalive every second) for as long
as it streamed, so the camera was switched off in the middle of an app session, and after a short
power-off it resumed that session with a live view that sends nothing. The videos that did come back
came right after our Bluetooth session had ended (app closed; between renewals). The app now does
as Mimo: connect, pair, wake, fetch the credentials, disconnect; it connects again only to wake the
camera when its network has been gone for 10 s. It also leaves the camera's Bluetooth requests
unanswered, as Mimo does. Verified 2026-09-25: five short power cycles in a row (including one 11 s
after the video had come back, the case that used to fail) each got video on the first connection,
~15 s after the camera was on. (`dji-vcam-cli --ble --ble-hold` keeps the link open, to reproduce.)

The same rule applies in the other direction: a Bluetooth session that starts while a datalink
streams takes that datalink's video away (seen on a quick Disconnect / Connect, when the camera's
Wi-Fi was still up and the datalink connected before the new Bluetooth session had finished; the
camera also refused Bluetooth service discovery while it streamed). The app therefore starts no
datalink connection while a Bluetooth session is open, and after hanging up it waits until the
camera has noticed (it advertises again, ~3.1-3.6 s) before the datalink may connect: a live view
started just before the camera drops the Bluetooth session dies with it.

### 3.12 The camera's Wi-Fi channel [VERIFIED 2026-09-25]

- **`0x07/0x2B` (set_wifi_frequency) moves the camera's 2.4 GHz access point**: payload the channel
  as a **u16 little-endian** (`01 00` = channel 1), answer `00`. A one-byte payload answers `ff`,
  an empty one `ff`. The access point restarts on the new channel (the bridge re-associates within
  ~2 s) and the camera keeps the channel afterwards.
- Send it over Bluetooth before the live view starts. Over the datalink during a live view it also
  moved the access point (no answer: it restarts at once), but no video came back afterwards.
- `0x07/0x44` (get_frequency) answers `00 00 01`: the band, not the channel; `0x07/0x10` sets the
  band (what DJI Mimo exposes, 2.4 / 5 GHz). `0x07/0x19` answers the country code (`ff` "FI").
- Why it matters: one evening the camera sat on channel 10 next to busy networks on 9, 11 and 13;
  the live view fell to a few fps and the bridge failed to send its acks (`to wifi ... dropped`).
  On channel 1 the same setup gave a steady 3.7 Mbit/s with no loss. A scan cannot see how busy a
  network is, only how loud: channel 10 carried only 1.3 times channel 1's interference.

### 3.13 The camera on your Wi-Fi network: Live Streaming mode [VERIFIED 2026-09-26]

Instead of offering its own access point, the camera can join an existing Wi-Fi network (DJI:
`dji_wifi_switch_sta_and_connect`), which DJI Mimo does for its RTMP livestream. **The live view
of 3.9 then works over that network**, at the camera's address on it, **in 1920x1080 at 30 fps**
(~3.6-4.1 Mbit/s) and with the same delay as through the camera's access point (measured with a
clock on 2026-09-26). No RTMP stream has to run. Reproduced with `dji-vcam-cli --join-network`.

**DJI Mimo's sequence** (Android HCI snoop log of a Mimo livestream setup on the camera, 2026-09-26;
all over Bluetooth, the network password and pairing identifier not reproduced here):

| # | App -> camera | Camera's answer | Notes |
|---|---|---|---|
| 1 | `00/2B 04 00` to `0xF0`, pairing `07/45` | as in a.5 | no `53/10` wake, no `07/07`/`07/0E` |
| 2 | `02/E1 1a` to `0x08` | `00` after ~2 s | **Live Streaming mode**; the screen says "Preparing to live stream" |
| 3 | `02/8E 00 01 1c 00` to `0x08`, `08/79 01` to `0x08` | `08/79`: the stored livestream settings (below) | |
| 4 | `07/AB` to `0x1B` (repeated) | `00`, then the network list `07/AC` (below) seconds later | optional for us |
| 5 | `07/47` + name + password (length-prefixed strings) to `0x07` | `00 00` | Mimo sent it twice: the first, during the scan, got no answer |
| 6 | `08/78` start (below) to `0x08`, `02/8E 01 01 08 00 01 00` to `0x01`, `02/8E 01 01 1a 00 01 01` to `0x08` | `00` each | the camera connects to the RTMP address |
| 7 | `00/2B 04 00` to `0xF0` every 2.5 s | | Mimo keeps the Bluetooth link the whole time |
| 8 | stop: `02/8E 01 01 1a 00 01 02` to `0x08` | `00` | |

**What the camera needs** (our tests, 2026-09-26):

- **Live Streaming mode first.** Outside it, `07/47` makes the camera drop its access point and try
  to join, but it never reaches the router; its answer (`01 ff`) came minutes later.
- **The scan is optional.** In Live Streaming mode the join answers `00 00` after ~10 s without it,
  ~1 s after a scan. Hidden networks join by name (and appear in the list once known).
- **The Bluetooth link must stay up**, with Mimo's keep-alive: hanging up sends the camera back to
  its access point within seconds. (The opposite of 3.11, which is about the access point.)
- **Stop** (`02/8E 01 01 1a 00 01 02`) ends the livestream, the join *and* Live Streaming mode.
  Switching to Video mode (`02/E1 01` to `0x08`; sent to `0x01` it gets no answer in this mode)
  also ends the join.
- The camera keeps both screens on while "preparing" (they time out during a running RTMP stream,
  as seen with Mimo); the Video mode's format list is replaced by the livestream's (the Camera
  settings panel's format changes snap back), stabilization changes work.
- The camera answers `07/0C` with its Wi-Fi MAC, the same on its access point and as a client:
  the app can find its address by MAC, or from its RTMP connection.

**Livestream start `08/78`** (Mimo, for the Action 5 Pro; Moblin's older layout is in section 4):
`01 8a 00 <resolution> <kbit/s: u16 LE> fe 01 00 00 00 00 7f 00` then JSON
`{"rtmpAddress":"rtmp:\/\/<host>:<port>\/<path>","watermark":0,"codec":"","EnhancedRTMP":false,"supportStopLive":false}`
(slashes escaped). Resolution `04` = 720p, `0a` = 1080p (as Moblin); Mimo offered 4000 and
6000 kbit/s. The camera **stores** these settings; `08/79 01` reads them back:
`00 01 8a 00 <resolution> <kbit/s> 00 01 00 00 00 00 7f 00 <address, NUL-padded>`. A start to a
closed port is accepted (`00`) and stores them too. **The preview stays 1080p** whatever is stored.
Unknown: `fe 01` (stored as `00 01`), the frame rate field.

**Network list `07/AC`** (pushed after `07/AB`): a 4-byte header (`01 11 00 00`; a short
follow-up push had `01 11 04 00`), then per network `[length incl. itself] 01 01 <band> <flag> 00
<name>`. Band: `01` 2.4 GHz, `02` 5 GHz (by the networks' names); the flag's meaning is unknown.

## 4. Verified A5P video alternative: RTMP push over BLE (Moblin)

[V-A5P per Moblin code comments: "Patch for OA5P …" (`DjiDevice.swift:415-425`); model-specific configure byte `0x1A` for A5P/360 (`:340-349`); `hasNewProtocol()` = true for A5P (`SettingsDjiDevice.swift:80-101`)]

The camera joins **your** Wi-Fi as a client (router or PC hotspot) and pushes RTMP to a URL, so the camera's AP is not involved. For OBS, run a local RTMP server (mediamtx, nginx-rtmp) and add it as a Media Source. The sequence is a state machine that advances on the response whose **msg id** matches (`DjiDevice.swift:241-265,275-435`). All frames go to `fff5`:

| # | state | DUML | target (sender→recv) | id | payload |
|---|---|---|---|---|---|
| 1 | pair | `0x07/0x45` | 02→07 | `0x8092` | `PackString("284ae5b8…bea3") + PackString("mbln")`. Sent when `fff4` notify is enabled (`:467-487`). |
| 2 | | ← `00 01` means go to 3. Otherwise wait: **any** later message is treated as approval (`:275-289`) | | | |
| 3 | cleaningUp | `0x02/0x8E` "stop stream" | 02→**08** | `0xEAC8` | `01 01 1a 00 01 02` (`DjiDeviceMessage.swift:153-159`) |
| 4 | preparingStream | `0x02/0xE1` | 02→08 | `0x8C12` | `1a` (= camera mode 0x1A "Live Streaming"; compare R-SDK enum `protocol_data_segment.md:220` and osmosis `MEDIA_PROTOCOL.md:759-773`) |
| 5 | settingUpWifi | `0x07/0x47` | 02→07 | `0x8C19` | `PackString(router SSID) + PackString(router password)`. The reply must be `00 00`, otherwise `wifiSetupFailed` (`:315-323`). |
| 6 | configuring | `0x02/0x8E` | 02→**01** | `0x8C2D` | `01 01 1a 00 01 <stab>`, where stab: off=0, RockSteady=1, HorizonSteady=2, RS+=3, HorizonBalancing=4 (`DjiDeviceMessage.swift:161-193`). The byte `0x1A` is A5P/360-specific; OA4/OA6 use `0x08`. |
| 7 | startingStream | `0x08/0x78` | 02→08 | `0x8C2C` | `00 2a 00 <res> <kbps:u16-LE> 02 00 <fps> 00 00 00 <urlLen:u8> 00 <url>`, with res 480p=`0x47`, 720p=`0x04`, 1080p=`0x0A` and fps 25=`2`, 30=`3` (`DjiDeviceMessage.swift:41-78,206-226`). `0x2A` is the "oa5" byte (legacy bodies use `0x2E`). |
| 7b | | `0x02/0x8E` "confirm start" | 02→08 | `0xEAC8` | `01 01 1a 00 01 01` (A5P "new protocol" patch, `:415-425`) |
| 8 | streaming | ← reply with id `0x8C2C`. Then status pushes `0x0D/0x02` (type `0x020D00`): battery % at `payload[20]` (`:429-446`, `DjiDeviceMessage.swift:195-204`) | | | |
| stop | | same frame as step 3 | | | wait for the id `0xEAC8` reply (`:103-112,448-453`) |

Notes:
- **Bitrates** offered: 2–20 Mbps. **FPS** 25 or 30 (`SettingsDjiDevice.swift:150-161`).
- **Codec** is not selectable on the A5P (`hasVideoCodec()` is false, `SettingsDjiDevice.swift:103-124`). Which codec the A5P pushes is **[UNVERIFIED]**. Moblin's system test asserts HEVC 1080p30 plus AAC 48 kHz, but the test does not name the camera model (`tests/suites/dji_camera.py:54-76`).
- **Frame size:** the 1-byte BLE length means the start frame must stay under 256 B, so the URL can be at most about 228 bytes.
- Watchdogs: 60 s for start, 10 s for stop (`DjiDevice.swift:128-154`). Moblin does not ACK `0x07/0x46`, and it does not arm `fff4` with `01 00`.

---

## (c) Open questions and risks

1. **AP live view on the A5P is unproven.** No source demonstrates UDP-9004 video from an Action 5 Pro. PocketShow's `0x00/0x4F` trigger is Pocket 3 only, and even there it is only a README claim. Expect to need a Mimo pcap.
2. **Codec and sub-header.** The 12-byte video sub-header is undocumented, and A5P live preview may be HEVC. Nothing in the sources says.
3. **ACK semantics.** The video cursor comes from status frame `[10:12]` in osmosis but from the type-0x02 header seq in PocketShow. The routing ack is seq−8 in osmosis and seq−32 in PocketShow. Byte 10 is `00` vs `60`. Session-id adoption differs too. Getting these wrong gives "session up, no data" (`DumlTransport.kt:16-20`).
4. **Write cliff after about 50 s** (Xtra) could stall a live-view heartbeat. Re-registering periodically may be needed.
5. **MTU 500 vs 517** conflict inside osmosis (a.2). Use 500.
6. **`0x07/0x46` ACK payload** (echo `01` vs `00`) is unresolved (a.5).
7. **`0x53/0x10` on the A5P:** unknown whether the A5P accepts it or answers `e0` like the Pocket 3. The AP came up for testers either way.
8. **Activation gate:** a never-activated body has no AP (measured on a Nano). The user must have activated the camera with Mimo once.
9. **Shared identifier:** using `284ae5b8…` may fight over the camera's remembered-pairing slot with Moblin or osmosis on the same camera. Mint your own and accept one on-screen tap.
10. **Xtra vs DJI firmware:** the Xtra runs the datalink on 10004, and camera-control cmdset `0x02` to receiver `0x01` gets **no reply** on the Xtra (`MEDIA_PROTOCOL.md:83,713-715`). Genuine DJI A5P behaviour there is not documented.
11. **AP vs station exclusivity:** ~~unverified~~ [VERIFIED 2026-09-26] the A5P drops its AP while it is a client of another network (3.13).
12. **Simpler alternative for OBS:** the A5P has a USB **UVC webcam** mode (R-SDK camera mode `0x23`, `protocol_data_segment.md:220`). If a cable is acceptable, it avoids all of the above.
13. Firmware drift: all of this is reverse-engineered and may change with firmware. The osmosis docs record several reversals (the `0x07/0x47` wake, the MTU, the port mapping).

---

## Appendix: reference byte vectors

Generated with the CRC parameters [CHECKED] above; `tools/test_duml.py` re-verifies several of them. Msg ids are arbitrary, since the camera echoes them. Frames marked *Mimo* are real captures.

```
BLE (fff5, write-without-response)
00/2b 04 00   (Mimo)  550f04a202f01bcb40002b04009ab9
00/2b 04 00           550f04a202f02b8040002b04002440
07/45 osmo            553304c202079280400745203238346165356238643736623333373561303461363431376164373162656133046f736d6fa0b4
53/10         (Mimo)  55110492021c1dcb40531000000000894a
07/07 GetWifiSsid     550d043302070780400707fbcd
07/0e GetWifiPass     550d043302070e8040070e5e01
07/46 ACK (echo 01)   550e046602073412c0074601a45c   <- bytes 6..7 must be the camera's request id

UDP 9004 (base=0x5a38, session=0x4c21)
handshake             3180214c000000dc385a0064006400c005140000640000019001c005140000640014006400c00514000064000101040102
cmd 00/88 (osmosis)   2f80214c405a05dd385a405a0000000001010000551b0475022800a0400088170046237c415050000000000002e6e8
ack type 0x04 (34 B)  2280214c000004cb00000000000000000000000000000000385a385a000000000000

Moblin RTMP push (A5P)
stop  02/8e -> 0x08   551304030208c8ea40028e01011a0001029219
prep  02/e1 [1a]      550e04660208128c4002e11a11df
wifi  07/47           5520047b0207198c400747084d79526f75746572097365637265743132335048   (SSID "MyRouter", pass "secret123")
cfg   02/8e -> 0x01   5513040302012d8c40028e01011a0001007516                             (stab off)
start 08/78           553704f902082c8c400878002a000a70170200030000001c0072746d703a2f2f3139322e3136382e312e31302f6c6976652f6f6135e192
confirm 02/8e         551304030208c8ea40028e01011a000101092b
```

---

## (d) DJI Mimo 2.12.1 APK analysis (2026-09-24)

The symbols of the app's native SDK library (`libdjisdk_jni.so`, 51k of them) give DJI's own
names for 321 DUML
commands. Reference lists in `docs/reference/`:

- `duml-command-map.txt`: cmd_set/cmd_id -> DJI request/response struct names (camera settings:
  exposure mode `0x02/0x1e`, shutter `0x02/0x28`, ISO `0x02/0x2a`, white balance `0x02/0x2c`,
  EV `0x02/0x2e`, color tone `0x02/0x3e`, working mode `0x02/0x10`, recording mode `0x02/0x6c`,
  `parameter_option` `0x02/0x8e`, take photo `0x02/0x01`, record `0x02/0x02`, audio `0x02/0x9f`...).
  Payload layouts are not in the names and still need working out per command.
- `dds-topics.txt`: XRCE-DDS topics carried by `0x00/0x99` (e.g. `camcap_eis` stabilization,
  `camcap_antiflicker`, `camcap_capture_aspect_type`); the Action 5 Pro publishes its settings here.
- `camera-keys.txt`: the SDK's camera key names.

Findings:

- **No live-view quality control for the Action 5 Pro (AC204) in Mimo.** The SDK keys
  `LiveViewQuality` (NORMAL/FINE/SFINE), `LiveViewOutputFormat` and `H1LiveViewResolutionFrameRate`
  (720p30/1080p30/720p60) are wired only to drone camera abstractions, never to `AC204CameraAbs`.
- Our start-sequence commands, by DJI's names: `0x00/0x88` = query_device_information (Mimo sends
  `17 00 00 23 00 'APP' 00x5 02`; bytes 2 and 4 are masked), `0x00/0x99` = XRCE-DDS pub/sub,
  `0x00/0x4F` = get_version_config (our 200 ms "heartbeat"), `0x00/0x81`/`0x82` are DM368
  low-level commands outside this SDK layer.
- `SendAppDecodeAbility` = `0x09/0xFD` to `0x48`, TLV list `[count]` + `[type u8][value u32-LE]`
  (1 = resolution, 2 = codec, 3 = bit depth, 4 = bitrate, 5 = frame rate). Tested with 1920x1080:
  **no reply and no effect** on the Action 5 Pro (stream stays 1280x720).
- `AppRequestIFrame` = `0x09/0xA8` to `0x48`: on-demand keyframe (useful for faster startup).
