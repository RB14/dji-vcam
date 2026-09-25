# DJI Osmo Action 5 Pro: camera controls (mode, record, format, exposure, colour, audio, status)

> Researched 2026-09-25 (desk research, camera off). Companion to [protocol-notes.md](protocol-notes.md),
> which covers the transport (BLE pairing, UDP 9004 datalink, DUML framing, live view). Living
> reference: update entries as our own captures confirm or contradict them.

Target body: DJI Osmo Action 5 Pro, product code **AC204** (`CameraType` 95), BLE model byte `0x15`.
Every command below is a DUML frame as defined in protocol-notes.md (a.3). Unless a row says
otherwise it goes **App `0x02` → Camera `0x01`, flags `0x40`**, and the reply comes back with the
addresses swapped, flags `0xC0`, the same seq, and a first payload byte `00` for success.

Evidence tags:

| tag | meaning |
|---|---|
| **[VERIFIED-CAPTURE]** | Seen on *this* camera in our own sessions (`tools/dji_liveview.py`, protocol-notes.md 3.9) |
| **[APK]** | Derived from DJI Mimo 2.12.1 (`dji.mimo`): the AC204 bindings in `libdjisdk_jni.so` and the decompiled Java |
| **[EXTERNAL]** | Another project or DJI document. The body it was verified on is given in brackets, e.g. [EXTERNAL A6] |
| **[GUESS]** | Inference that nothing above supports directly. Test before relying on it |

**About the Mimo captures.** `captures/20260924-215412/` contains **no camera traffic**.
`mimo.pcap` (18,875 packets, 200 s) holds only home-LAN traffic from the phone:
no 192.168.2.x host and zero CRC-valid DUML frames anywhere in any payload. `btsnoop_hci.log` holds
ATT traffic with non-DJI BLE peripherals (ASCII `DeviceType;`, `GetLight,7;`) and no DUML frame. Mimo
crashed on the Pixel (SIGSEGV tombstone), so no session was recorded. As a result, **nothing in this
file is [VERIFIED-CAPTURE] from a Mimo session**. That tag only covers what our own tool observed.
The best substitute is a set of Mimo captures made by OpenPocketCine on the **Osmo Action 6**
(the A5P's successor, same camera SDK family in Mimo), tagged [EXTERNAL A6]. Wherever that evidence
and the APK agree, confidence is high. Experiment 1 in section 6 closes this gap for the A5P.

---

## 0. Sources

| key | what | version |
|---|---|---|
| APK native | `lib/arm64-v8a/libdjisdk_jni.so` from the Mimo 2.12.1 APK. Cited as `Function@vaddr` (vaddr = file offset). Symbols are unstripped. `dji_cmd_base_req<1,set,id,…>` template arguments give each handler's command. | Mimo 2.12.1 |
| APK java | jadx output of the app's Java code. Cited as `jadx cNN/<path>`. The SDK enums live in `jadx c11/sources/dji/sdk/keyvalue/value/camera/*.java`, the Mimo camera UI in `jadx c10/sources/dji/lomo/fpv/…` | same |
| OPC | erik-sutton95/OpenPocketCine. Handbook pages are Mimo captures with frame numbers (Action 6 fw V01.02.0521, Mimo 2.12.0). Paths are relative to `handbook/src/content/docs/` | `1820923` (2026-09-24) |
| osmosis | KonradIT/osmosis `MEDIA_PROTOCOL.md` (Nano, Pocket 3, Xtra captures) | `6992036` |
| moblin | eerimoq/moblin `Moblin/Integrations/Dji/DjiDevice/*.swift` (A5P-verified RTMP flow) | `2a908f1` |
| R-SDK | dji-sdk/Osmo-GPS-Controller-Demo `docs/protocol_data_segment.md`, `protocol/dji_protocol_data_structures.h` (official, lists A5P) | `92fe23e` |
| OSMO-Remote | PANDAJSR/OSMO-Remote `docs/LIVEVIEW_FINDINGS.md` (tcpdump of Mimo live view on an A5P) | `f543230` |
| djictl | xaionaro-go/djictl `pkg/duml/*.go`, `pkg/djiwifi/constants.go` | `ddeced5` |
| DJI web | A5P specs and FAQ (dji.com/osmo-action-5-pro), user manual v1.0, release notes (dl.djicdn.com), DJI support livestream table (customId en-us03400006728) | 2026-09 |

AC204 key inventory (what Mimo can read or change on this camera) [APK]:
`dji::sdk::ac204::CameraCharacteristics()@0x18ce3f4` lists 171 keys.
`AC204CameraAbs::WillSetup()@0x1cba034` binds the setters and actions.
`AC204CameraAbs::ObserverTopicPack()@0x1cbf814` binds the read-back topics.
`AC204CameraAbs::GetAbsSenderHostID()@0x1cba024` returns 1, so the receiver is the camera, `0x01`.
There is no AC204 "UI service" or audio abstraction, so camera-menu items that are not in this list
(screen brightness, beeps, voice prompts, wind-noise reduction) are **not controllable from Mimo**
on this body.

---

## TL;DR

1. **Three mechanisms cover every Mimo control.**
   - **Dedicated `0x02/xx` commands** to `0x01`: mode `02/E1`, format `02/18`, record `02/02`,
     photo `02/01`, exposure `02/1E` `02/2A` `02/28` `02/2E`, white balance `02/2C`, colour `02/42`,
     texture `02/38`, noise reduction `02/44`, anti-flicker `02/46`, codec `02/AB`, and others.
   - **The generic parameter command `0x02/0x8E`**: SET `01 01 <pid u16-LE> <len> <value>`,
     GET `00 01 <pid u16-LE>`. It covers stabilization (pid `0x0008`), FOV (`0x0009`), auto-ISO
     ceiling (`0x000F`), Daily/Sport (`0x0030`), loop recording (`0x0018`), voice control, and more.
   - **State read-back** through named XRCE-DDS subscriptions `0x00/0x99` sent to `0x28`
     (`cam_status`, `cam_video_param_v2`, `cam_expo_param`, `cam_image_effect`, `cam_record_time`,
     `cam_storage`, `camcap_*`), plus the unsolicited pushes `0x02/0x80`, `0x02/0xDC` and `0x0D/0x02`.
2. **Byte layouts are well established.**
   - [APK] Taken from the handler code for this exact model.
   - [EXTERNAL A6] Mimo captures on the Action 6 confirm them value by value.
   - A5P confirmation is still pending: the commands have never been sent to *this* camera.
3. **Transport.** All of it rides the UDP 9004 datalink we already run (type-5 command packets).
   Moblin sends `0x02/0x8E` and `0x02/0xE1` to an A5P over **BLE** successfully, so a BLE-only
   control path is plausible. The official R-SDK (`0xAA` framing, BLE) adds a small, documented
   set: mode `1D04`, record `1D03`, key presses `0011`, and a 2 Hz status push `1D02` with mode,
   resolution, fps, EIS, record time, remaining capacity, temperature and battery.
4. **A 1080p live view is unlikely over the camera's own Wi-Fi.**
   - Mimo has no live-view quality control for AC204 [APK].
   - Mimo's own A5P preview is 1280x720 H.264 [EXTERNAL OSMO-Remote], and every other Osmo body
     previews at 720 lines [EXTERNAL OPC].
   - Realistic 1080p paths: RTMP push (Mimo offers 1080p at 3 or 6 Mbit/s), UVC webcam (1080p30),
     USB-C DisplayPort. Section 3.12 and experiment 4 (section 6) list cheap tests that could still prove otherwise.

---

## 1. Summary

"Read" names where the current value comes from: `topic@off` is an offset into the 0x00/0x99 value
blob (section 2.3), `GET pid` is a `0x02/0x8E` GET, `1D02@off` is the R-SDK status push.
"DL" = UDP datalink, "BLE" = DUML on fff5/fff4.

| # | function | set / action | read | transport | confidence |
|---|---|---|---|---|---|
| 1 | Shooting mode (video, photo, slow-mo, timelapse, hyperlapse, SuperNight, ...) | `02/E1 [mode]` | `cam_status@4`; `1D02@0` | DL [EXTERNAL A6]; BLE likely (Moblin sends `02/E1` over BLE, to `0x08`) | [APK] + [EXTERNAL A6, Nano, R-SDK] |
| 1b | Mode switch, official | R-SDK `1D04` | R-SDK `1D02@0` | BLE (R-SDK) | [EXTERNAL R-SDK, lists A5P] |
| 2 | Record start / stop / pause / resume | `02/02 [01/00/02/03]` | `cam_status@0` bits 3-4; `02/80@0`; `1D02@1` | DL [EXTERNAL A6]; R-SDK `1D03` on BLE | start/stop: [APK]+[EXTERNAL A6]; pause/resume: [APK] only |
| 3 | Take photo | `02/01 [type]` (`01` single, `00` stop) | `cam_status@0` bits 0-2 | DL | [APK] + [EXTERNAL A6] |
| 4 | Recording time | none | `cam_record_time@0`; `02/80@29`; `1D02@5` | DL / BLE | [APK] + [EXTERNAL A6, Nano] |
| 5 | Resolution + fps (+ aspect, which is part of the resolution code) | `02/18 [res][fps] 00 00 00` | `cam_video_param_v2@0/@1`; list `camcap_video_format`; `1D02@2/@3` | DL | [APK] + [EXTERNAL A6] |
| 5b | Slow-motion multiplier | `02/18 [res][fps] 00 [ratio u16]`; also pid `0x001F` | `cam_video_param_v2`; GET pid `0x001F` | DL | [APK] + [EXTERNAL A6] |
| 6 | Stabilization (Off / RS / RS+ / HS / HB) | `02/8E` pid `0x0008` | GET pid `0x0008`; list `camcap_eis`; `1D02@4` | DL [EXTERNAL A6]; BLE (Moblin, pid `0x001A`, live mode) | [APK] + [EXTERNAL A6] |
| 6b | Stabilization scene Daily / Sport | pid `0x0030` | `cam_status@0` bit 12; GET pid `0x0030` | DL | [APK] + [EXTERNAL A6] |
| 7 | FOV (Ultra Wide / Wide / Standard-dewarp / ...) | pid `0x0009` | GET pid `0x0009`; list `camcap_fov` | DL | [APK] + [EXTERNAL A6, Nano] |
| 8 | Exposure mode Auto / Manual | `02/1E [mode] 00` | `cam_expo_param@7` | DL | [APK] + [EXTERNAL A6] |
| 9 | ISO | `02/2A [idx]` | `cam_expo_param@5` (setting), `@16` (actual) | DL | [APK] + [EXTERNAL A6] |
| 10 | Auto-ISO ceiling | pid `0x000F` | GET pid `0x000F` | DL | [APK] + [EXTERNAL A6, Nano] |
| 11 | Shutter | `02/28` (7 B) | `cam_expo_param@20-22` (actual); A6 also `@2-4` (configured) | DL | [APK] + [EXTERNAL A6] |
| 12 | Auto shutter limit (ShutterMax) | pid `0x0034` | `shutter_param@0` | DL | [APK] + [EXTERNAL A6] |
| 13 | EV compensation | `02/2E [idx]` | `cam_expo_param@6` (A6 capture) or `@15` (what the APK key reads), see 3.6 | DL | [APK] + [EXTERNAL A6] |
| 14 | AE lock | `02/68 [bool]` | `cam_expo_param@9` | DL | [APK] |
| 15 | Spot metering | pid `0x0015` | GET pid `0x0015` | DL | [APK] |
| 16 | Anti-flicker | `02/46 [af]` | `cam_image_effect@3` | DL | [APK] |
| 17 | White balance Auto / Kelvin | `02/2C [mode][K/100 u16][tint i16]` | `cam_image_effect@4..@11` | DL | [APK] + [EXTERNAL A6] |
| 18 | Colour (Normal 8/10-bit, HLG, D-Log M) | `02/42 [color]` | `cam_image_effect@2`; list `camcap_color_mode` | DL | [APK] + [EXTERNAL A6, Nano, P3] |
| 19 | Texture (sharpness) | `02/38 [s8]` (GET `02/39`) | `cam_image_effect@15` | DL | [APK] + [EXTERNAL A6] |
| 20 | Noise reduction | `02/44 [s8]` (GET `02/45`) | `cam_image_effect@14` | DL | [APK] + [EXTERNAL A6] |
| 21 | Video codec H.264 / H.265 | `02/AB [codec] 00` | `cam_video_param_v2@8` low nibble | DL | [APK] |
| 22 | Pro mode (advanced controls) | pid `0x0000` | `cam_status@0` bit 10 | DL | [APK] + [EXTERNAL A6] |
| 23 | Mic channel (mono / stereo / ...) | pid `0x0020` | GET pid `0x0020` | DL | [APK]; A6 shows GETs only |
| 24 | Wind-noise reduction, audio DSP | `02/9F` (whole blob) | `02/A0` GET | DL | [EXTERNAL A6/P3]; **not bound for AC204** in Mimo |
| 25 | Audio levels, wireless mics | none | `cam_audio_status_v2` | DL | [APK] |
| 26 | Battery % | none | `0D/02@20`; `1D02@37` | DL + BLE | [APK] + [EXTERNAL A5P Moblin] |
| 27 | Storage (SD + internal) | format: `02/72` | `cam_storage`; `02/DC`; `02/80@5/@9`; `1D02@15/@19/@23` | DL / BLE | [APK] + [EXTERNAL Xtra, Nano] |
| 28 | Temperature | threshold `00/44` → `0x28` | `1D02@30`; temperature push (cmd unknown); `temp_curve@1` | BLE (R-SDK) / DL | [EXTERNAL R-SDK] + [APK] |
| 29 | Timecode | none (camera menu only) | `timecode_info@3..6`; `audio_timecode_status@0` | DL | [APK] + [EXTERNAL A6] |
| 30 | Loop recording, pre-record exit | pids `0x0018`, `0x003D` | GET pid `0x0018`; `1D02@35` | DL | [APK] + [EXTERNAL A6] |
| 31 | Timelapse / hyperlapse parameters | `02/6C` (16 B) | `cam_lapse_param` | DL | [APK] + [EXTERNAL A6] |
| 32 | Photo size, ratio, format, burst | `02/12`, `02/14`, `02/16`, `02/48`, `02/FB`, `02/5E` | `cam_photo_param_new` | DL | [APK] + [EXTERNAL A6] |
| 33 | Voice control on/off, language | pids `0x000A`, `0x000E` | GET | DL | [APK] + [EXTERNAL A6] |
| 34 | Camera UI language | `02/56` → **`0x1C`** | `02/57` → `0x1C` | DL | [APK] |
| 35 | Custom modes (save / apply) | pid `0x002F` | `cam_custom_mode_params` | DL | [APK] + [EXTERNAL A6] |
| 36 | HDR video, video-quality enhancement | pids `0x0028`, `0x0035` | GET; `v_quality_enhance_status@0` | DL | [APK] |
| 37 | Quick-switch button, highlight tag | R-SDK `0011` key `0x02` | none | BLE (R-SDK) | [EXTERNAL R-SDK] |
| 38 | Sleep / wake | R-SDK `001A [3]`; wake-up advert | `1D02@28` | BLE (R-SDK) | [EXTERNAL R-SDK] |
| 39 | Camera clock | `00/6A` → `0x28` | none | DL | [EXTERNAL Nano] (protocol-notes 3.7) |
| 40 | Live-view keyframe / enable | `09/A8` → `0x41` (A6, Nano) | video | DL | [EXTERNAL A6, Nano]; untested on A5P |
| 41 | Live-view resolution / bitrate | **none exists for AC204** | none | none | [APK] + [VERIFIED-CAPTURE] |

---

## 2. Command families

### 2.1 Addressing, replies and errors

- Requests go `0x02 → 0x01` with flags `0x40` (cmd_type 2, "needs ack"). [APK] Every handler builds
  `dji_cmd_base_req<1, set, id>` and writes receiver type 1, index 0. [EXTERNAL A6] The same holds
  on the wire (OPC `devices/action-6/commands.md:12-15`).
- **Exceptions** [APK]:
  - camera language `02/56` and `02/57` go to **`0x1C`**;
  - the temperature threshold `00/44` goes to **`0x28`**;
  - DDS subscriptions `00/99` go to **`0x28`** [EXTERNAL A6, Nano] (OPC `devices/action-6/connection.md:150-153`, osmosis `MEDIA_PROTOCOL.md:665-669`);
  - live-view enable `09/A8` goes to **`0x41`** on A6 and Nano [EXTERNAL] (OPC `protocol/live-view.md:50-56`).
- Setter reply: `[ret u8]`. On a DDS subscribe the reply carries `plen=10`. Known `ret` values:
  - `00` OK;
  - `d8` resource not ready;
  - `d9` wrong state, e.g. already recording, or a photo command in video mode;
  - `df` wrong parameter;
  - `e3` missing parameter;
  - `e0` not supported;
  - no reply at all means the receiver does not exist.

  Sources: osmosis `MEDIA_PROTOCOL.md:717-727`. Also seen: `E1` during rapid WB retries
  (OPC `devices/action-6/settings.md:419-423`) and `D6` while the camera is busy or in playback
  (OPC `protocol/live-view.md:58`).
- **An ACK means received, not applied.** Confirm the change on the topic push or with a GET
  [EXTERNAL A6] (OPC `devices/action-6/commands.md:16-18`).
- Over the datalink, command replies can arrive in pktType **`0x03`** datagrams as well as `0x01`.
  The window ACK must echo the newest `0x03` seq in the download group, otherwise those replies
  stop [EXTERNAL] (OPC `protocol/live-view.md:115-117`).

### 2.2 Generic parameter command `0x02/0x8E` (parameter_option)

[APK] `SendOneTlvBufferSetPack@0x1beb4c4`, `SendOneTlvBufferGetPack@0x1beb0e0`, response parser
at `0x170192c`. [EXTERNAL A6] `devices/action-6/settings.md:348-381`, osmosis `MEDIA_PROTOCOL.md:789-810`.

```
SET  req  01 01 <pid u16-LE> <len u8> <value[len]>        rsp  <ret>
GET  req  00 01 <pid u16-LE>                               rsp  <ret> 00 01 <pid u16-LE> <len u8> <value[len]>
```

The first byte is the operation (`01` set, `00` get); the second byte `01` is probably the TLV count
[GUESS]. The Mimo parser checks that the reply's pid and length match the request. Example:
the GET FOV reply on the A6 was `00 00 01 09 00 01 05` [EXTERNAL A6].

Pids bound for AC204 [APK]. "len" is the value length Mimo sends and expects back; values pass
through unchanged unless noted.

| pid | key | len | values | also [EXTERNAL A6] |
|---|---|---|---|---|
| `0x0000` | CameraAdvanceMode ("Pro") | 1 | 0 normal, 1 professional | yes |
| `0x0006` | VideoStreamIQQuality | 1 | 1 NORMAL, 2 HIGH; the handler rejects other values | no |
| `0x0008` | CameraEISCtrl / CameraEISEnabled | 1 | see 3.4 | yes |
| `0x0009` | FOV | 1 | see 3.5 | yes |
| `0x000A` | Voice control | 1 | 0 off, 1 on | yes |
| `0x000E` | Voice-control language | 1 | 0 Chinese, 1 English | yes |
| `0x000F` | ISOAutoMax | 1 | 1=100, 2=200, 3=400, 4=800, 5=1600, 6=3200, 7=6400, 8=12800, 9=25600 | yes |
| `0x0014` | CameraRotateMode / State (GET only) | 3 | orientation; Mimo polls it about 15 Hz (osmosis `MEDIA_PROTOCOL.md:803`) | no |
| `0x0015` | SpotMeteringEnabled | 1 | bool | no |
| `0x0018` | LoopRecordDuration | 4 | u32-LE seconds: 0 off, 300, 1200, 3600, 65535 max | yes |
| `0x001F` | SlowMotionSpeed | 1 | multiplier, 1..255 | no |
| `0x0020` | Mic accessory audio channel | 1 | 0 default, 1 mono, 2 stereo, 3 spatial, 128 safe-track, 129 stereo R/L | GET only |
| `0x0028` | HDR video | 1 | bool | no |
| `0x0029` | Short-video duration | 9 | `01 <secs u32-LE> 00 00 00 00` | no |
| `0x002F` | Save custom mode (action) | 2 | APK: u16 index. A6: `slot, op` with save `00`, apply `03`, delete `02` | yes |
| `0x0030` | Steady preferred (Daily / Sport) | 1 | 0 Daily, 1 Sport | yes |
| `0x0033` | ImageAdjust | 1 | 0 original, 1 stream-media, 2 custom | no |
| `0x0034` | ShutterMax (auto shutter limit) | 1 | 0 = frame-rate based, 1 = 1/100-1/120, 2 = 1/200-1/240, 3 = 1/400, 4 = 1/800, 5 = 1/1600 | yes |
| `0x0035` | Video-quality enhancement | 1 | bool (UI "Original" / "Enhanced") | no |
| `0x003D` | Exit pre-record (action) | 1 | `00` | no |

Pids that exist in the SDK but are **not** bound for AC204 [APK]:
- `0x001A`: Moblin's "live" EIS on the A5P [EXTERNAL A5P];
- `0x0024` selfie follow;
- `0x0038` selfie mirror;
- `0x0039` beauty;
- `0x003A`/`0x003B` AF;
- `0x0046` frame/orientation policy (A6 uses it);
- `0x0047`/`0x0048` film tone;
- `0x004C` vocal boost.

### 2.3 State read-back: DDS subscriptions `0x00/0x99`

Sent to receiver `0x28` with flags `0x40`, one frame per name [EXTERNAL Nano, A6], layout per osmosis
(`app/.../camera/CameraSession.kt:2253-2275`, `MEDIA_PROTOCOL.md:665-699`):

```
subscribe  02 02 00 00 | sub_id u32-LE | 00 00 00 | (name_len+6) u16-LE | name_len u16-LE | name | 00 00 00 00
push       02 06 00 00 | idx u32-LE    | 00 00 00 | total_len u16-LE    | name_len u16-LE | name | 00 x6 | value_len u16-LE | value
```

`sub_id` increments per subscription. The camera pushes the value once, then on every change
(osmosis says about 0.5-1 Hz). `camcap_*` topics are the capability lists for the *current* mode and
format. They change after format and mode changes, so re-read them [EXTERNAL A6]
(`devices/action-6/settings.md:143-157`). Capability-list layout:
`[ver u8][len u16][count u8][count x entry]`, one byte per entry, except `camcap_video_format`,
which uses 3-byte `[res][fps][flags]` entries. Honour `count` and ignore trailing bytes [APK]
(`KeyCameraEISCtrlRangeTopicPush@0x1d0f1a8`) [EXTERNAL A6] (`settings.md:227-246`).

Topics Mimo reads for AC204, and the layouts its handlers use [APK] (`AC204CameraAbs::ObserverTopicPack`;
wire names from the `dji_topic_*` constructors). Offsets are into the value blob.

**`cam_status`** (9 B)

| off | field |
|---|---|
| 0 | u16 flags. Bits 0-2: taking photo. Bits 3-4: record state (non-zero = recording). Bit 8: playback. Bit 9: writing file. Bit 10: Pro mode. Bit 12: IsSteadyPreferred (presumably Sport). Bits 13-14: ImageAdjust |
| 2 | camera type (95 = AC204) |
| 3 | work mode (0 photo, 1 record, 2 playback, 3 download, ...) |
| 4 | shooting mode, same codes as `02/E1` (A6 confirms, OPC `devices/action-6/modes.md:43-48`) |
| 7, 8 | pre-recording indicators (`@7 == 5`, `@8 == 1`) [APK, uncertain] |

**`cam_video_param_v2`**

| off | field |
|---|---|
| 0 | resolution code (3.3) |
| 1 | fps code (3.3) |
| 3 | bit 0: speed-ratio flag, meaning unclear |
| 8 | low nibble: codec, 0 H.264, 1 H.265 |

**`cam_expo_param`** (46 B on A6)

| off | field |
|---|---|
| 0 | u16 aperture setting |
| 2-4 | configured shutter [EXTERNAL A6] |
| 5 | ISO setting index |
| 6 | configured EV [EXTERNAL A6] |
| 7 | exposure mode |
| 9 | AE lock |
| 13 | u16 actual aperture x100 (280 = f/2.8) |
| 15 | EV index read by the APK key; metered EV per A6, see 3.6 |
| 16 | actual ISO (u32 in the APK, u16 per OPC) |
| 20-22 | actual shutter: u16 (bit 15 = "1/x", bits 0-14 = integer), then a decimal byte |

**`shutter_param`**: `@0` ShutterMax code.

**`cam_image_effect`** (16 B)

| off | field |
|---|---|
| 0 | saturation |
| 1 | hue |
| 2 | colour |
| 3 | anti-flicker |
| 4 | WB mode |
| 5 | WB K/100 (A6: u16 at 5-6 = current value; 9 = selected mode; 10-11 = selected K/100) |
| 14 | noise reduction, s8 |
| 15 | texture, s8 |

**`cam_record_time`** (6 B): u16 seconds at `@0`. When bit 15 of `@4` is set, `@4` bits 0-14 hold
bits 16-30 of the seconds.

**`cam_storage`**: `@0` high nibble = current store (0 SD, 1 internal). `@2` low nibble = entry
count. Entries are 18 B from `@4`:

| entry off | field |
|---|---|
| +0 | store type |
| +1 | bit 0 present; bits 1-4 state (CameraSDCardState: 0 normal, 1 not inserted, 4 format needed, 8 full, ...) |
| +2 | u32 total MB |
| +6 | u32 free MB |
| +10 | u32 photos left |
| +14 | u32 video seconds left |

**Other topics**

| topic | layout |
|---|---|
| `cam_lapse_param` | `@0` timelapse file format, `@1` u16 interval, `@5` u32 duration, `@9` u16 hyperlapse speed, `@11` u16 dynamic ratio, `@13` u32 recorded s, `@17` u32 frames |
| `cam_photo_param_new` | `@3` size, `@4` ratio, `@5` quality, `@6` storage format, `@7` burst |
| `cam_audio_status_v2` | `@3` s16 volume; `@7` 8 x s16 mic levels; wireless mic 1 mute `@27` bit 0 and pair status `@51`; mic 2 at `@58` / `@82` |
| `timecode_info` | `@3..@6` HH MM SS FF [EXTERNAL A6] (`devices/action-6/device-settings.md:88-91`) |
| `audio_timecode_status` | `@0` bit 0 = timecode on |
| `temp_curve` | `@1` threshold (1 low, 2 high); `@2` bit 0 = high threshold supported |
| `v_quality_enhance_status` | `@0 != 0` = on |
| `cam_fov` | 4 x u32: width, height, ratio, focal length. **Not** the FOV setting |

Capability topics Mimo binds for AC204 (`ObserverRangeUpdate@0x1cbda80`): `camcap_base` (mode list),
`camcap_video_format`, `camcap_fov`, `camcap_iso`, `camcap_iso_auto_max`, `camcap_eis`,
`camcap_color_mode`, `camcap_wb`, `camcap_antiflicker`, `camcap_sharpness`, `camcap_denoise`,
`camcap_shutter`, `camcap_shutter_max`, `camcap_loop_video_duration`, `camcap_hyperlapse_ratio`,
`camcap_slowmotion_ratio`, `camcap_timelapse_duration`, `camcap_common`, photo lists.
The full list of 53 names the official app subscribes to is in osmosis `MEDIA_PROTOCOL.md:697-699`.

**Not published as a topic** [APK]: the current EIS, FOV, auto-ISO ceiling, slow-motion speed,
loop duration and mic channel. Mimo reads those with a `0x02/0x8E` GET (2.2).

[VERIFIED-CAPTURE] Once our datalink session is registered, the camera sends `00/99` at about
50/s even though we subscribe to nothing, plus the pushes `02/80`, `02/dc`, `0d/02`, `00/f1` and
`00/74`, and replies to our own `00/4f`, `00/88` and `00/81`. These are the counts in the "duml
frames seen" summary of `tools/dji_liveview.py` runs on 2026-09-24; the payloads were not logged.

### 2.4 Unsolicited DUML pushes

| push | layout | tag |
|---|---|---|
| `0x02/0x80` camera state, ~10 Hz | Nano layout: `@0` flags (bit 7 recording, bit 30 playback), `@4` 1 = video-type mode, `@5`/`@9` u32 active-store total/free MB, `@13` u16 photos left, `@17` remaining s, `@29` u16 elapsed record s, `@57` shooting mode (osmosis `MEDIA_PROTOCOL.md:841-926`). The APK struct agrees on `@4` (work mode) and `@9` (SD free) and adds `@0` bit 9 SD inserted and bits 10-13 SD state | [EXTERNAL Nano] + [APK]; A5P offsets unverified |
| `0x02/0xDC` storage | 40 B on A5P/Xtra: `@2` store count, `@6`/`@10` SD total/free MB, `@24`/`@28` internal total/free MB (osmosis `MEDIA_PROTOCOL.md:865-890`) | [EXTERNAL Xtra] |
| `0x0D/0x02` battery, ~1 Hz, sender `0x05` | `@1` u16 mV, `@5` i32 mA, `@20` percent, `@28` bit 2 present, `@32` charging (Moblin `DjiDeviceMessage.swift:195-204`; APK `AC204BatteryAbs::ObserverPushPack@0x18ce108`) | [EXTERNAL A5P] + [APK] |
| temperature status | u16 `@0`: low byte must be `0x42`; bits 10-11 = 0 normal, 1 warning, 2 exception, 3 serious (`KeyTemperatureSystemStatusPush@0x1c4707c`). **Which cmd carries it is unknown.** Candidate: the unexplained `0x00/0xF1` push we see [GUESS] | [APK] |

### 2.5 Official R-SDK (SOF `0xAA`, BLE only)

The framing and CRCs are in protocol-notes.md a.4. The official command set is small but documented
for the A5P. The camera reports `device_id` `0xFF44` in the connection exchange. In commands,
`device_id` is the *sender's* ID: the demo sends the constants `0xFF330000` for `1D04` and
`0x33FF0000` for `1D03`. It needs the R-SDK connection handshake `0x00/0x19`, which shows an accept
prompt on the camera. Coexistence with our DUML pairing is untested.

| cmd | payload | reply |
|---|---|---|
| `1D04` mode switch | `device_id u32 \| mode u8 \| reserved x4` (mode = `02/E1` codes; the demo sends reserved `01 47 39 36`) | `ret u8 \| 00 x4` |
| `1D03` record | `device_id u32 \| 0 start / 1 stop \| 00 x4` | `ret` |
| `0011` key report | `key u8 \| mode u8 \| value u16`. Key `01` record, `02` QS (quick switch; a short press while recording adds a highlight tag), `03` snapshot | `ret` |
| `001A` power mode | `0` normal, `3` sleep | `ret` |
| `1D05` status subscribe | `push_mode u8` (3 = periodic + on change) `\| 20 \| 00 x4` | `1D02` pushes at 2 Hz |
| `1D02` status push (38 B) | `@0` mode, `@1` status (0 screen off, 1 live, 2 playback, 3 shooting, 5 pre-record), `@2` resolution, `@3` fps idx, `@4` EIS, `@5` u16 record s, `@8` photo ratio, `@9` countdown, `@11` u16 timelapse interval, `@13` u16 timelapse duration, `@15` u32 free MB, `@19` u32 photos left, `@23` u32 record s left, `@27` custom mode, `@28` power mode, `@29` next mode (QS), `@30` temperature (0 ok, 1 warm, 2 too hot to record, 3 shutting down), `@31` u32 photo countdown ms, `@35` u16 loop s, `@37` battery % | none |

Sources: R-SDK `docs/protocol_data_segment.md:148-313`, `protocol/dji_protocol_data_structures.h:29-195`.
Highlight tag via QS: rhoenschrat/DJI-Remote `logic/command_logic.c:798-1005` [EXTERNAL A4/A5P/A6].

---

## 3. Functions

### 3.1 Camera mode

`0x02/0xE1 [mode u8]` → `0x01`. The value passes through raw [APK] (`QuickCameraModeSet@0x1c08714`).
Read back from `cam_status@4`, `02/80@57` (Nano) or `1D02@0`. The list of modes available right now
comes from `camcap_base` / `camcap_mode_profile`.

| mode | code | evidence |
|---|---|---|
| Slow motion | `00` | APK, R-SDK, osmosis, A6 |
| Video | `01` | all |
| Timelapse | `02` | all |
| Photo | `05` | all; A6 confirmed (`devices/action-6/modes.md:43-50`) |
| Hyperlapse | `0A` | all |
| Live streaming | `1A` | R-SDK; Moblin sends it to `0x08` before RTMP |
| UVC webcam | `23` | R-SDK |
| SuperNight (low-light video) | `28` | R-SDK ("Super night scene in Osmo Action 5 Pro"), A6 |
| Subject tracking | `34` | R-SDK |
| Panorama | `0C` | osmosis (Nano) |
| Other SDK values: 3 video HDR, 4 AEB, 6 burst, 8 interval, 9 countdown, 25 loop record, ... | | APK `QuickCameraMode.java`; **do not send unless `camcap_base` lists them** |

- Example (Video): `550e0466020134124002e1014df2`. Photo: `550e0466020134124002e10569b4`.
- **Never sweep this enum:** enumerating `02/E1` froze a Nano until it was power-cycled
  (osmosis `MEDIA_PROTOCOL.md:687-688`).
- A mode change can silently change other state. On the A6, SuperNight switched the colour from
  D-Log M to Normal 10-bit and Photo switched it to `00`, with no colour SET sent
  (`devices/action-6/modes.md:55-60`).
- `0x02/0x10 [work mode]` (0 photo, 1 record, 2 playback, 3 download) also exists
  [APK] (`CameraWorkModeSet@0x1c2b16c`), but it is **not** the shooting mode. Values 2 and 3
  presumably leave capture [GUESS], so do not use it. Osmosis describes a different "work mode" on
  `02/02 [0-3]` (`MEDIA_PROTOCOL.md:751-757`); that collides with record start/stop and does not
  match Mimo's AC204 code.

### 3.2 Record, photo, recording time

| action | frame payload | notes |
|---|---|---|
| Start recording | `02/02 01` | `StartRecordAction@0x1be6e6c` [APK]; A6 `devices/action-6/settings.md:258-266`. Example `550e04660201341240020201bc31` |
| Stop recording | `02/02 00` | `StopRecordAction@0x1be8160`. If pre-recording, Mimo instead sends TLV pid `0x003D = 00` (`StopRecordWithCheckPrecordAction@0x1be8acc`) |
| Pause / resume | `02/02 02` / `02/02 03` | [APK] only (`PauseRecordingAction@0x1bf5c74`, `ResumeRecordingAction@0x1bf5f84`); whether the A5P honours it is unknown |
| Take photo | `02/01 [type]` | type follows the current mode: 1 single, 2 HDR, 4 burst, 5 AEB, 6 interval, 7 pano, ... (`PhotoTypeSDKTypeToProtocolType`). `02/01 01` also starts a timelapse and `02/01 00` stops it [EXTERNAL A6]. In a video mode the reply is `d9` |

- Replies are `00`, but the ACK is not the state change. Record start sets the recording bits after
  0.6-0.9 s (osmosis `MEDIA_PROTOCOL.md:736-742`). After stop there is a finalizing phase of 0.7-2.4 s
  (osmosis `MEDIA_PROTOCOL.md:744-749`; A6 `devices/action-6/settings.md:263-269`).
- Wait on the state bits: `cam_status@0` bits 3-4, `02/80@0` bit 7, or `1D02@1 == 3`.
- `[01]` while already recording answers `df`. The command is not a toggle.
- Recording time: `cam_record_time` u16 `@0` seconds, `02/80@29` or `1D02@5`. Remaining time:
  `cam_storage` entry `+14`, `02/80@17` or `1D02@23`.

### 3.3 Resolution, aspect ratio, frame rate

`0x02/0x18` → `0x01`, payload `[res u8][fps u8] 00 00 00` [APK]
(`VideoResolutionFrameRateSet@0x1c1f3d4`: identity maps; unmapped values pass through)
[EXTERNAL A6] 30 formats set and read back (`devices/action-6/settings.md:47-110`). For slow motion,
bytes 3-4 hold the speed ratio as a u16: `0A 07 00 04 00` = 1080p, 120 fps, 4x
(`VideoResolutionFrameRateAndSpeedRatioSet@0x1c2068c`; OPC `devices/action-6/modes.md:505-541`).
The aspect ratio is part of the resolution code; there is no separate video-aspect command.
Read back from `cam_video_param_v2@0/@1`. The legal pairs for the current mode are in
`camcap_video_format`, as 3-byte `[res][fps][flags]` entries.

| resolution | code | | fps | code |
|---|---|---|---|---|
| 1080p 16:9 (1920x1080) | `0A` (10) | | 24 | `01` |
| 2.7K 16:9 (2688x1512) | `2D` (45) | | 25 | `02` |
| 4K 16:9 (3840x2160) | `10` (16) | | 30 | `03` |
| 2.7K 4:3 (2688x2016) | `5F` (95) | | 48 | `04` |
| 4K 4:3 (3840x2880) | `67` (103) | | 50 | `05` |
| 1080p 9:16 (1080x1920) | `42` (66) | | 60 | `06` |
| 2.7K 9:16 (1512x2688) | `43` (67) | | 100 | `0A` |
| 4K 9:16 | `6D` (109) | | 120 | `07` |
| 720p (RTMP only) | `04` (4) | | 200 | `13` |
| | | | 240 | `08` |

Sources: codes from R-SDK `1D02` (the official A5P list), APK `VideoResolution.java` /
`VideoFrameRate.java`, and A6. The A5P spec sheet lists 4K and 2.7K in 4:3 and 16:9 and 1080p in
16:9 up to 240 fps; 9:16 appears in subject tracking. Always trust `camcap_video_format` over
this table.

- Examples: 4K 16:9 at 30 fps `551204c70201341240021810030000002d76`;
  1080p at 60 fps `551204c7020134124002180a0600000092e0`.
- [VERIFIED-CAPTURE] **The live view follows only the aspect ratio**: 16:9 gives 1280x720 and 4:3
  gives 960x720. A format change restarts the preview stream with a new sequence number and a new SPS
  (protocol-notes.md 3.9).
- A format change also narrows or widens the EIS, FOV, shutter and zoom lists. On the A6 the camera
  temporarily forced HS to RS plus Wide at 100/120 fps and restored them afterwards
  (`devices/action-6/controls.md:66-97`). **Re-read EIS and FOV after every format SET.**

### 3.4 Stabilization

`0x02/0x8E` pid **`0x0008`**, 1 byte → `0x01` [APK] (`CameraEISCtrlSet@0x1c35be0`, `CameraEISCtrlGet`)
[EXTERNAL A6] all five values set, acknowledged and read back with GET (`devices/action-6/controls.md:52-64`).

| value | mode (Mimo label) |
|---|---|
| `00` | Off |
| `01` | RockSteady |
| `02` | HorizonSteady |
| `03` | RockSteady+ (Mimo label "Steady") |
| `04` | HorizonBalancing |
| `07` | HorizonCorrection (HB+) [APK only] |
| `08` | RS Auto [APK only] |

The same values appear in R-SDK `1D02@4` (0-4) and Moblin (`DjiDeviceMessage.swift:161-193`).
Enum: APK `CameraEISCtrlMode.java`; labels: APK `EisItemType.java` plus `res/values/strings.xml`.

- Examples: SET RS+ `551304030201341240028e01010800010318b7`; SET Off `551304030201341240028e0101080001008385`;
  GET `551104920201341240028e00010800d47e` (reply `00 00 01 08 00 01 <v>`).
- To read the current value, use the GET. It is not in any topic for AC204. The legal values are
  in `camcap_eis`; also `1D02@4` over R-SDK.
- Constraints (DJI FAQ): HorizonSteady only at 1080p or 2.7K 16:9, 60 fps or less. HorizonBalancing
  at 16:9 up to 4K, 60 fps or less. On the A6, HB and HS forced FOV to Standard (`02`).
- Scene **Daily / Sport**: pid **`0x0030`** (0/1) [APK]+[EXTERNAL A6] (`devices/action-6/settings.md:383-400`).
  Read it from `cam_status@0` bit 12. The A6 switched to Sport by itself in manual exposure.
- Moblin uses pid **`0x001A`** (`01 01 1a 00 01 <stab>` → `0x01`) to set EIS for RTMP on the A5P.
  Mimo's AC204 keys never use `0x1A`, so it is presumably the live-stream EIS [GUESS].

### 3.5 FOV

`0x02/0x8E` pid **`0x0009`**, 1 byte [APK] (`FOVSet@0x1c33090`) [EXTERNAL A6, Nano].

| value | APK `CameraFOV` | A6 label (Mimo capture) |
|---|---|---|
| `00` | SUPER_WIDE | Ultra Wide |
| `01` | NORMAL | Wide |
| `02` | LINEAR | Standard (dewarp) |
| `05` | NATURAL_WIDE | Natural Wide (osmosis: Natural-Wide on Nano) |
| `06` | FLAT | Portrait Flat (A6 portrait mode) |
| `03`, `04` | NARROW, EXTREME_WIDE | not seen on Action bodies |

The DJI FAQ names Wide and Standard (dewarp) for the A5P. Whether Ultra Wide and Natural Wide exist
on this body is unconfirmed; use `camcap_fov` for the live list. To read, send GET pid `0x0009` (`cam_fov` holds geometry, not this enum).
Example SET Standard: `551304030201341240028e0101090001022aba`.

### 3.6 Exposure

All go to `0x01` [APK]; [EXTERNAL A6] value ladders in `devices/action-6/settings.md:202-231,508-560`
and `controls.md:211-256`.

| setting | frame payload | encoding | read back |
|---|---|---|---|
| Mode | `02/1E [mode] 00` | 1 auto (program), 4 manual; SDK also has 2 shutter priority and 3 aperture priority | `cam_expo_param@7` |
| ISO | `02/2A [idx]` | 0 Auto, 3=100, 4=200, 5=400, 6=800, 7=1600, 8=3200, 9=6400, 10=12800, 11=25600, 12=51200 | `@5` index, `@16` actual ISO |
| Auto-ISO ceiling | TLV pid `0x000F` | 4=800 ... 9=25600 (2.2). In D-Log M the ceiling drops to 12800 on A6 | GET pid `0x000F` |
| Shutter | `02/28 01 <u16 v> <dec> 00 00 40` | `v` bit 15 = "1/x", bits 0-14 = integer; `dec` = decimal digits (1/12.5: v=`800c`, dec 5). Examples: 1/200 = `01 c8 80 00 00 00 40`, 1/120 = `01 78 80 00 00 00 40`. 1 s and longer: bit 15 clear. Leading `01` and trailing `40` are constants of unknown meaning. Full table: `ShutterSpeedSDKTypeToProtocolStruct@0x14f7e54` | `@20-22` actual; A6 configured `@2-4` |
| Auto shutter limit | TLV pid `0x0034` | 0 = frame-rate based, 1 = 1/100-1/120, ... 5 = 1/1600 | `shutter_param@0` |
| EV | `02/2E [idx]` | 16 = 0 EV, 1/3 EV per step, 1 = -5 ... 31 = +5. A6 accepts `07`..`19` (±3 EV) | see below |
| AE lock | `02/68 [bool]` | | `@9` |
| Spot metering | TLV pid `0x0015` bool | AC204 has no centre/average metering key (`02/22` is not bound) | GET |
| Anti-flicker | `02/46 [af]` | 0 auto, 1 60 Hz, 2 50 Hz, 3 off. **Mimo only sends it when the exposure mode is 1 or 3** (`AntiFlickerSet@0x1c0e234`) | `cam_image_effect@3` |

- EV read-back conflict. The A6 captures show `cam_expo_param@6` = configured EV (all 51 writes
  echoed) and `@15` = metered EV. Mimo's AC204 key reads `@15`. Show `@6` as the setting and `@15`
  as the meter until the A5P confirms (experiment 6).
- Manual mode on the A5P uses fixed f/2.8, so aperture fields are constant. ISO and shutter only take
  effect in mode 4. Entering manual can change other state: on the A6 it switched to Sport and
  aperture strategy 1.
- Examples: Manual `550f04a20201341240021e04001ce7`; ISO 400 `550e04660201341240022a056b9a`;
  1/120 `5514046d0201341240022801788000000040b3b1`; EV 0 `550e04660201341240022e1027ba`.

### 3.7 White balance

`0x02/0x2C [mode u8][K/100 u16-LE][tint i16-LE]` → `0x01` [APK] (`WhiteBalanceSet@0x1c18844`)
[EXTERNAL A6] (`devices/action-6/settings.md:402-450`).
- Modes: 0 Auto, 6 Manual/Kelvin; the SDK also has 1 sunny, 2 cloudy, 3 water, 4 incandescent,
  5 fluorescent, 7 natural, 8 underwater.
- Kelvin range 2000-10000 in 100 K steps (`camcap_wb`). Auto = `00 00 00 00 00`. Send tint `0`:
  the A6 always sent zero and no tint UI exists.
- Read back: `cam_image_effect@4` mode and `@5` K/100. On the A6, `@5-6` is the live measured or
  current value, `@9` the selected mode and `@10-11` the selected K/100. The current value lags
  after a SET, so wait for it to settle.
- Send at most one request in flight and coalesce slider moves to about 100 ms. The A6 answered `E1`
  to rapid retries.
- Example 3400 K: `551204c70201341240022c062200000061bd`; Auto: `551204c70201341240022c0000000000dc30`.

### 3.8 Colour profile, texture, noise reduction

| setting | frame payload | values | read back | tag |
|---|---|---|---|---|
| Colour | **`02/42 [c]`** (digital_filter; *not* `02/3E`) | `00` Normal 8-bit, `3F` Normal 10-bit, `3C` HLG 10-bit, `3D` D-Log M 10-bit | `cam_image_effect@2`; list `camcap_color_mode` | [APK] `CameraColorSetImpl@0x1c29630` (checks against `camcap_color_mode`); [EXTERNAL A6] `3F`/`3D` (`settings.md:249-256`); Nano/P3 `00`/`3C`/`3D` (OPC `protocol/commands.md:50`) |
| Texture (sharpness) | `02/38 [s8]`; GET `02/39` | -2..+2 (`FE`..`02`) | `cam_image_effect@15` | [APK] + [EXTERNAL A6] (`settings.md:452-476`) |
| Noise reduction | `02/44 [s8]`; GET `02/45` | -2..+1 on A6 (`camcap_denoise`) | `cam_image_effect@14` | same |
| Codec | `02/AB [c] 00` | 0 H.264, 1 H.265 (second byte never set by Mimo; send `00`) | `cam_video_param_v2@8` | [APK] |
| Video-quality enhancement | TLV pid `0x0035` | bool | `v_quality_enhance_status@0` | [APK] |
| Pro mode | TLV pid `0x0000` | 0/1. Pro gates manual controls, and on the A6 it can restore colour by itself | `cam_status@0` bit 10 | [APK] + [EXTERNAL A6] |

D-Log M lowers the ISO ceiling (A6: 25600 to 12800), and some modes (SuperNight, Photo) force a
colour. Always re-read `cam_image_effect@2` after a mode change. Mimo labels AC204 Normal as
"Normal 8bit" (string `ac204_fpv_color_normal_8bit_title`, APK `FpvConfigHelper$FpvCameraColorSettings`).
Example D-Log M: `550e0466020134124002423d358c`.

### 3.9 Audio

- **Mic channel**: TLV pid `0x0020`, 1 byte (0 default, 1 mono, 2 stereo, 3 spatial, 128 safe-track,
  129 stereo R/L) [APK] (`MicrophoneAccessoryAudioChannelSet@0x1caaa04`). The A6 shows 8,690 GETs of
  this pid and no SET (OPC `devices/action-6/commands.md:93-98`), so the SET semantics are unconfirmed.
- **Wind-noise reduction, directional audio, gain**: **not bound for AC204** in Mimo 2.12.1.
  - Other models use `02/A0` (GET) and `02/9F` (SET) with one opaque blob, 26-27 B:
    `WindNoiseLevelGet@0x1dc6324` and `AudioRecordingGainGet@0x1e3ae9c` build `02/A0` [APK].
  - Captures of other bodies place the wind flag at `@2` (`1A`/`18`) and directional audio at `DA` /
    `3A` / `BA`, but the layout is model-specific. **Always GET, change one bit, and SET the whole
    blob** (OPC `protocol/commands.md:56-57`) [EXTERNAL A6/P3]. Unknown on the A5P.
- **Levels and wireless mics** (read only): `cam_audio_status_v2` (2.3) [APK].

### 3.10 Status

| item | best source | fallbacks |
|---|---|---|
| Battery % | `0x0D/0x02@20` push (~1 Hz) [EXTERNAL A5P Moblin] [APK] | `1D02@37` (R-SDK) |
| Charging / voltage / current | `0x0D/0x02` `@32` / `@1` / `@5` [APK] [EXTERNAL Nano] | |
| Storage | `cam_storage` per store: total, free, photos left, seconds left [APK] | `0x02/0xDC` (40 B on A5P) [EXTERNAL Xtra]; `02/80@5/@9`; `1D02@15/@23` |
| Recording, playback, busy | `cam_status@0` bits [APK] | `02/80@0` bit 7 / bit 30 [EXTERNAL Nano]; `1D02@1` |
| Mode, format, EIS | `cam_status@4`, `cam_video_param_v2`, GET pid `0x0008` | `1D02@0/@2/@3/@4` |
| Temperature | `1D02@30` (0 ok, 1 warm, 2 cannot record, 3 shutting down) [EXTERNAL R-SDK] | temperature push (2.4, cmd unknown); heat threshold set `00/44 0d [1 low / 2 high]` → `0x28`, read `temp_curve@1` [APK] |
| Timecode | `timecode_info@3..6` HH:MM:SS:FF [APK] [EXTERNAL A6] | `audio_timecode_status@0` bit 0 = on |
| Remaining record time | `cam_storage` `+14` | `02/80@17`; `1D02@23` |
| Firmware version | `0x00/0x00` to `0x48` (protocol-notes.md 3.7, osmosis `MEDIA_PROTOCOL.md:701-706`) | |

### 3.11 Other settings Mimo can change on AC204

| setting | command | notes / tag |
|---|---|---|
| Loop recording | TLV pid `0x0018`, u32-LE s (0, 300, 1200, 3600, 65535) | [APK] + [EXTERNAL A6] (`modes.md:884-915`) |
| Timelapse / hyperlapse | `02/6C`, 16 B: `@0` type (4 timelapse, 5 motionlapse, 11 hyperlapse), `@2` file format (0 video, 2 JPEG+video, 3 RAW+video), `@3` u16 interval (0.1 s) **or** hyperlapse ratio (0 auto, 2, 5, 10, 15, 30), `@5` u32 duration s, `@15` motionlapse mode. Mimo does read-modify-write | [APK] (`GetCurrentTimeLapseSettingReq@0x1bec01c`) + [EXTERNAL A6] (`modes.md:603-620,714-745`). Read `cam_lapse_param` |
| Photo size / ratio | `02/12 [size][ratio]` (size 3 M, 4 L; ratio 0 4:3, 1 16:9) | [APK] + [EXTERNAL A6] |
| Photo format | `02/16` (0 RAW, 1 JPEG, 2 RAW+JPEG) | [APK] + [EXTERNAL A6] |
| Burst | `02/48 [n]`; time-limited burst `02/FB 01 05 00 <ms u32> <n>`; AEB `02/5E [offset][count]` | [APK] + [EXTERNAL A6] |
| Custom modes | TLV pid `0x002F` (save); list in `cam_custom_mode_params` | [APK] + [EXTERNAL A6] (`modes.md:916-950`) |
| Voice control | pids `0x000A` (on/off), `0x000E` (0 zh, 1 en) | [APK] + [EXTERNAL A6] (`device-settings.md:102-121`) |
| Camera UI language | `02/56 [lang]` → **`0x1C`** (0 zh-Hans, 1 zh-Hant, 2 en, 3 de, 4 ko, 5 ja, 6 fr, 7 es, 8 it, 9 pt, 10 ru, 11 th, 12 tr, 13 id, 14 pl) | [APK] |
| HDR video | TLV pid `0x0028` bool | [APK]; not in the A5P manual [GUESS: may be refused] |
| Histogram push | `02/60 [bool]` | [APK] |
| Heat threshold | `00/44 0d [1 low / 2 high]` → `0x28` | [APK] |
| Format card | `02/72 [store?]` (1 B, probably 0 SD / 1 internal); 50 s timeout | [APK], payload uncertain, **destructive** |
| Storage location | `02/DA` (only a 7-byte zero prelude traced) | [APK], incomplete |
| Camera clock | `00/6A 01 00 <unix u64> <utc offset min i16> <tz len> <tz>` → `0x28` | [EXTERNAL Nano] |
| Quick switch, highlight | R-SDK `0011` key `02` (2.5) | [EXTERNAL R-SDK] |
| Not exposed by Mimo for AC204 | screen brightness, beeps, voice prompts, screen/orientation lock, auto-off, GPS (phone-side only on A6), wind-noise reduction | [APK] no key bound; commands unknown |

### 3.12 Live-view resolution and bitrate (the 1080p question)

Facts:
- [VERIFIED-CAPTURE] Our AP preview is H.264 High, 1280x720, about 30 fps, about 3.5 Mbit/s.
  Only the aspect ratio changes it (protocol-notes.md 3.9). `SendAppDecodeAbility`
  (`09/FD`, 1920x1080) got no reply and had no effect.
- [EXTERNAL A5P] Mimo's own A5P preview is also 1280x720 H.264 (OSMO-Remote `docs/LIVEVIEW_FINDINGS.md:19-20`).
- [EXTERNAL] Every other Osmo body previews at 720 lines: A6 720x720 AVC, Pocket 4 1280x720 HEVC,
  Nano and Pocket 3 720p. "Recording 4K 50p does not raise the SoftAP monitor rate"
  (OPC `protocol/live-view.md:8-19,60`).
- [APK] Mimo has **no live-view quality key for AC204**. `LiveViewQuality`, `LiveViewOutputFormat`
  and `H1LiveViewResolutionFrameRate` are bound only to drone cameras, and
  `FpvConfigHelper$FpvLiveViewSettings` does not list AC204.
- [APK] The only stream-quality-sounding key bound for AC204 is **`VideoStreamIQQuality`** (pid `0x0006`,
  1 NORMAL / 2 HIGH). On the Action 2 its UI is recording-oriented ("Standard Mode" / "Power
  Reserve") (`VideoQualityHelper.java`). For AC204 it only gates RS+ in the EIS menu
  (`DJIEisSpinnerView.o()`).
- [EXTERNAL A6/Nano] Mimo enables live view with `09/A8 00 04 02 00 00 00 00 00 00 00` → `0x41`. The
  same frame is the IDR request. Send it once and never every second, because that resets the GOP
  (OPC `protocol/live-view.md:34-72`). Our PocketShow trigger (`00/81`, `00/82`, `00/4F`) works
  differently. The `04 02` bytes are unexplained and could encode a profile [GUESS].
- [EXTERNAL] djictl reads handshake bytes `64 00` as "quality 1-100, observed 100" and `14 00` as a
  20 ms frame interval (`pkg/djiwifi/constants.go`). Unverified.

Real 1080p paths [EXTERNAL DJI]:
1. **RTMP push**, camera in station mode (protocol-notes.md section 4). Mimo offers the A5P 480p
   (1/2 Mbit/s), 720p (2/4) and 1080p (3/6); DJI support, customId en-us03400006728.
   - Moblin allows 2-20 Mbit/s.
   - djictl and Moblin disagree on the A5P start byte (`0x2E` vs `0x2A`), so test both.
2. **UVC webcam** over USB, mode `0x23`: up to 1080p30 MJPG (Magewell KB 0008020015).
3. **USB-C DisplayPort** output: 4K from firmware 01.02.03.30 (A5P release notes).

---

## 4. How to apply a setting safely

1. **Preconditions.**
   - A registered datalink session: `00/81`, then `00/88` about every 1 s; our live-view loop
     already does this. Mimo's own 1 Hz beat is `00/88 1a 00 00 00 01` (osmosis
     `CameraSession.kt:94-98`).
   - Camera not in playback: `cam_status@0` bit 8, or `02/80` bit 30. **Never send `02/0C` or
     `02/10` 2/3 while live view is wanted.**
   - No body menu open that blocks the app. The A6 showed "Device in timecode setting. Unable to use
     app" (`device-settings.md:78-81`).
2. **Not while recording** for mode, format, colour, codec or EIS changes. Expect `d9`. Check
   `cam_status@0` bits 3-4 first.
3. **Capabilities first.** Subscribe to `camcap_*` for the current mode and only offer values listed
   there. Lists change with mode and format (2.3). Never probe unknown values: a `02/E1` sweep
   bricked a Nano until it was power-cycled.
4. **Order dependent settings.**
   - mode `02/E1` → format `02/18` → EIS/FOV (pids 8/9) → exposure mode → ISO / shutter / EV → WB →
     colour / texture / NR.
   - A later step can be undone or constrained by an earlier one: SuperNight forces the colour;
     HB/HS force Standard FOV; 100/120 fps drops HS; Manual forces Sport; D-Log M lowers the ISO ceiling.
   - Anti-flicker is only accepted in exposure mode 1 or 3.
5. **One request in flight.**
   - Wait for the `0xC0` reply with the same DUML seq: about 10-100 ms, time out at about 1 s.
   - On a timeout, resend with the **same** seq, up to 3 times (Mimo retries the same way, OPC).
   - Coalesce sliders (EV, WB, ISO) to 100 ms or more.
6. **Confirm on state, not on the ACK.** Use the matching topic field, or a TLV GET for pids that
   have no topic (EIS, FOV, ISO ceiling, loop). The A6 delivers the readback 50-200 ms after the ACK,
   and a push can even arrive before the ACK.
7. **Expect video hiccups.**
   - Format, colour, record and EIS SETs can pause or restart the preview encoder: new SPS, and the
     video seq jumps. The video ACK must accept the jump; this is already handled
     (protocol-notes.md 3.9).
   - Do not answer a stall by re-sending `09/A8` every second.
8. **Write cliff.** A session stops accepting writes after about 40-70 s unless it is re-registered
   (osmosis `MEDIA_PROTOCOL.md:642-644`). Our 1 Hz `00/88` should cover it; verify (experiment 9).
9. **Datalink replies may ride pktType `0x03`.** ACK them (2.1), or GET replies silently stop.
10. **BLE.**
    - Pace fff5 writes at least 10 ms apart.
    - Keep frames under 256 B, since the 1-byte length encoders are used.
    - Answer every inbound `0x40` request (protocol-notes.md a.5).
    - R-SDK frames (`0xAA`) share fff4/fff5 with DUML (`0x55`); demultiplex on the first byte.
11. **Destructive actions** (`02/72` format card, custom-mode delete) need explicit user confirmation
    in the UI.

---

## 5. Example frames

All frames are App `0x02` → receiver, flags `0x40`, seq `0x1234` (arbitrary; the camera echoes it).
CRCs were computed with `tools/duml.py`, and every frame decodes cleanly with `duml.decode()`.
Wrap each one in a type-5 datalink packet, or write it to fff5.

```
02/8E GET EIS (pid 08)            551104920201341240028e00010800d47e
02/8E GET FOV (pid 09)            551104920201341240028e000109000c67
02/8E SET EIS RockSteady (01)     551304030201341240028e0101080001010a94
02/8E SET EIS RockSteady+ (03)    551304030201341240028e01010800010318b7
02/8E SET EIS HorizonBal. (04)    551304030201341240028e010108000104a7c3
02/8E SET FOV Wide (01)           551304030201341240028e010109000101b188
02/8E SET FOV Standard (02)       551304030201341240028e0101090001022aba
02/8E SET Sport (pid 30 = 01)     551304030201341240028e010130000101203d
02/8E SET ISO ceiling 1600        551304030201341240028e01010f0001050f85
02/8E SET loop 300 s (pid 18)     551604fc0201341240028e01011800042c0100006f83
02/E1 mode Video                  550e0466020134124002e1014df2
02/E1 mode Photo                  550e0466020134124002e10569b4
02/E1 mode Slow motion            550e0466020134124002e100c4e3
02/E1 mode Timelapse              550e0466020134124002e102d6c0
02/E1 mode Hyperlapse             550e0466020134124002e10a9e4c
02/E1 mode SuperNight             550e0466020134124002e1288e4e
02/02 record start                550e04660201341240020201bc31
02/02 record stop                 550e046602013412400202003520
02/01 take photo                  550e04660201341240020101d41b
02/18 4K 16:9 @30                 551204c70201341240021810030000002d76
02/18 4K 4:3 @30                  551204c70201341240021867030000000253
02/18 1080p @60                   551204c7020134124002180a0600000092e0
02/18 1080p @120 slow-mo 4x       551204c7020134124002180a07000400499b
02/1E exposure Manual             550f04a20201341240021e04001ce7
02/1E exposure Auto               550f04a20201341240021e0100a499
02/2A ISO Auto                    550e04660201341240022a00c6cd
02/2A ISO 400                     550e04660201341240022a056b9a
02/28 shutter 1/200               5514046d0201341240022801c88000000040c173
02/2E EV 0                        550e04660201341240022e1027ba
02/2E EV +0.7                     550e04660201341240022e123599
02/2C WB 3400 K                   551204c70201341240022c062200000061bd
02/2C WB Auto                     551204c70201341240022c0000000000dc30
02/46 anti-flicker 50 Hz          550e046602013412400246022122
02/42 colour Normal 8-bit         550e046602013412400242005366
02/42 colour D-Log M              550e0466020134124002423d358c
02/42 colour HLG                  550e0466020134124002423cbc9d
02/38 texture -1                  550e046602013412400238ff9f64
02/44 noise reduction -1          550e046602013412400244fffb3d
02/AB codec H.265                 550f04a2020134124002ab01005b2a
00/99 -> 0x28 sub cam_status      552a049c0228341240009902020000df69000000000010000a0063616d5f7374617475730000000024fc
00/99 -> 0x28 sub cam_video_param_v2   553204060228341240009902020000e06900000000001800120063616d5f766964656f5f706172616d5f763200000000f1f6
00/99 -> 0x28 sub cam_expo_param  552e04a70228341240009902020000e169000000000014000e0063616d5f6578706f5f706172616d0000000087ca
00/99 -> 0x28 sub cam_image_effect 553004970228341240009902020000e26900000000001600100063616d5f696d6167655f656666656374000000009b4d
00/99 -> 0x28 sub cam_record_time 552f04630228341240009902020000e369000000000015000f0063616d5f7265636f72645f74696d65000000003a2d
00/99 -> 0x28 sub cam_storage     552b04580228341240009902020000e469000000000011000b0063616d5f73746f72616765000000007a7a
00/99 -> 0x28 sub camcap_eis      552a049c0228341240009902020000e569000000000010000a0063616d6361705f65697300000000ce5f
```

R-SDK (`0xAA`, BLE fff5). The builder was checked against the official example frame
(`protocol_data_segment.md:165`). `device_id` and the reserved bytes are copied from the demo's
`logic/command_logic.c:214-218,308-312`:

```
1D05 subscribe status (mode 3, 2 Hz)  aa1800010000000010004d8e1d05031400000000d2a49342
1D04 switch to Video                  aa1b000100000000110058ee1d04000033ff0101473936fce45f4e
1D03 start recording                  aa1b0001000000001200581e1d030000ff3300000000006b9dd3a5
0011 QS button short press            aa160001000000001300011e001102010000c658da3b
```

---

## 5b. Results on the Action 5 Pro (2026-09-25)

- **Status topics work as documented**: subscribing to the topics of section 2.3 over the datalink
  (receiver `0x28`) returned the camera's mode, format (resolution/fps), codec, stabilization, FOV,
  exposure mode, ISO setting, EV, auto-ISO limit, anti-flicker, white balance, colour profile,
  texture, noise reduction, battery and storage, decoded with the layouts above. Whether EV is `@6`
  or `@15` still needs a check against the camera screen (experiment 6).
- **No keyframe request** (`09/A8` to `0x41`/`0x08`/`0x48`, `02/B3`): see protocol-notes.md 3.10.
- Setters from the app's panel: pending a round-trip test on the camera (experiment 3).

## 6. Open questions / experiments to run with the camera on

Run them in this order. Each one is read-only or reversible unless marked otherwise.

1. **Status dump and diff (read-only; closes most gaps).**
   - Extend `tools/dji_liveview.py` with a `--dump-duml FILE` option that logs every inbound DUML
     frame as time, sender, cmd and hex, including pktType `0x03`.
   - Subscribe (to `0x28`) to `cam_status`, `cam_video_param_v2`, `cam_expo_param`, `shutter_param`,
     `cam_image_effect`, `cam_record_time`, `cam_storage`, `cam_audio_status_v2`, `timecode_info`,
     `temp_curve` and all the `camcap_*` names in 2.3.
   - On the camera touchscreen, change the mode, format, EIS, FOV, colour, WB and EV one at a time,
     then start and stop a recording.
   - Diff the pushes to confirm every offset in 2.3 and 2.4 for the A5P. This also covers `02/80`
     (recording bit, `@57` mode), `02/DC` and `0D/02`, and settles which push carries the
     temperature status (decode the unexplained `00/F1`).
2. **TLV GET sweep of known pids (read-only).**
   - Send `02/8E 00 01 <pid>` for each pid in 2.2: 00, 06, 08, 09, 0A, 0E, 0F, 14, 15, 18, 1F, 20,
     28, 30, 33, 34, 35.
   - Record reply codes and values. This shows which pids the A5P implements (`e0` vs data) and gives
     the current EIS/FOV.
   - Also GET `02/A0`, the audio blob, to learn its A5P length.
3. **Setter round-trip on the datalink (reversible).**
   - Each setter: EIS (pid 08: 01, then 00), FOV (pid 09), Daily/Sport (pid 30), `02/E1` Photo then
     Video, `02/18` 1080p60 then back, `02/42` D-Log M then back, `02/2C` 5600 K then Auto,
     `02/2E` +0.7 then 0, `02/1E` Manual then Auto.
   - For each one, log the ACK code, the readback latency and whether the preview restarts.
   - Finally record start/stop (`02/02`) and pause/resume (`02/02 02` / `03`).
4. **Live-view enable, Mimo style (reversible).**
   - Send `09/A8 00 04 02 00 00 00 00 00 00 00` to `0x41` (also try `0x08` and `0x48`) on a
     registered session **without** the PocketShow trigger. Does video start, and does it produce an
     IDR on demand?
   - If yes, try single-byte variants of bytes 1-2 (`04 02`), one at a time and seconds apart,
     watching the SPS for a size change.
   - In the same session: GET/SET pid `0x0006` (VideoStreamIQ 1 → 2), and vary the handshake's `64 00`
     "quality" fields. Measure the effect on bitrate and resolution.
5. **Control over BLE only (no Wi-Fi).**
   - After pairing (`07/45` → `00 01`), send `02/8E` GET pid 08, then `02/E1`, then `02/02` start/stop
     to `0x01` on fff5.
   - Check the replies arrive on fff4 and that `00/99` subscriptions to `0x28` push over BLE.
   - This decides whether the app can offer settings without the AP up.
6. **EV readback offset.** Set EV to +1.0 (`02/2E 13`) in Auto, then check whether `cam_expo_param@6`
   or `@15` holds `13`, and whether the other one drifts with the scene.
7. **R-SDK coexistence.**
   - With our DUML pairing active, send the R-SDK `0x00/0x19` connect (`verify_mode 1`) and accept
     it on the camera.
   - Subscribe `1D05` and record `1D02`. Compare mode, resolution, fps, EIS, battery and temperature
     against the DDS values.
   - Check whether the datalink stays up.
8. **RTMP 1080p path (the likely 1080p answer).**
   - Run the Moblin sequence (protocol-notes.md section 4) to a local mediamtx at 1080p, 6000 kbit/s.
   - Try the start byte `0x2A`, then `0x2E`, and pid `0x001A` stabilization.
   - Measure latency, codec (H.264 vs HEVC) and whether the AP can stay up at the same time.
9. **Long-session writes.** At 60 s, 120 s and 300 s into a live-view session, send one reversible
   setter and confirm it still answers. This checks whether our 1 Hz `00/88` defeats the write cliff.
10. **Audio blob mapping.**
    - GET `02/A0`. Toggle wind-noise reduction on the camera menu, GET again and diff.
    - Only then try `02/9F` with the whole blob and one bit flipped, then restore the original.
11. **HDR video (pid 0x28), mic channel (pid 0x20), camera language (`02/56` → `0x1C`).**
    Confirm accept vs `e0` and the visible effect.
12. **Datalink reply path.** Verify that TLV GET replies arrive in pktType `0x03`, and that our
    window ACK advances the download cursor to that seq. Fix `tools/datalink.py` if the replies stall.
