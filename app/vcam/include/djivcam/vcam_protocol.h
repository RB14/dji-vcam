// Frame hand-over between the DJI VCam app and its virtual-camera media source (Windows).
//
// The media source runs inside the Windows Camera Frame Server service (Local Service, session 0).
// It CREATES a named, pagefile-backed section in the Global namespace (which a service may do)
// with a DACL that lets authenticated users write; the app, in the user session, merely OPENS it
// and publishes frames. Layout: a 4 KiB header, then kSlotCount NV12 frame slots. Each slot has a
// sequence number used as a seqlock (odd while the writer fills it), so the reader never shows a
// half-written frame.
#pragma once

#include <cstddef>
#include <cstdint>

namespace djivcam::vcam {

// COM class of the media source ({87c6960c-3c27-4ce3-855d-c1f91a423df2}).
inline constexpr wchar_t kSourceClsid[] = L"{87c6960c-3c27-4ce3-855d-c1f91a423df2}";
inline constexpr wchar_t kFriendlyName[] = L"DJI VCam";
// The Frame Server (session 0) instance of the media source uses the Global section; instances loaded
// inside a camera app (user session, e.g. DirectShow clients) read the app's session-local one.
inline constexpr wchar_t kSectionName[] = L"Global\\dji-vcam-frames-v1";
inline constexpr wchar_t kLocalSectionName[] = L"Local\\dji-vcam-frames-v1";
// Authenticated users read/write (the app writes), Everyone read, AppContainer and
// less-privileged AppContainer read (camera consumers such as the Windows Camera app, browsers).
inline constexpr wchar_t kSectionSddl[] = L"D:P(A;;GRGW;;;AU)(A;;GR;;;WD)(A;;GR;;;AC)(A;;GR;;;S-1-15-2-2)";

inline constexpr std::uint32_t kMagic = 0x43564A44;  // 'DJVC'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kWidth = 1280;
inline constexpr std::uint32_t kHeight = 720;
inline constexpr std::uint32_t kFrameRate = 30;
inline constexpr std::uint32_t kSlotCount = 3;
inline constexpr std::size_t kHeaderSize = 4096;
inline constexpr std::size_t kFrameSize = std::size_t{kWidth} * kHeight * 3 / 2;  // NV12
inline constexpr std::size_t kSectionSize = kHeaderSize + kFrameSize * kSlotCount;
// Without a new frame for this long, the source shows its "no signal" frame.
inline constexpr std::uint64_t kStaleAfterMs = 1000;

struct SectionHeader {
    std::uint32_t magic;             // written last by the first writer
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    volatile std::int64_t latest_slot;  // slot holding the newest complete frame, -1 if none
    volatile std::int64_t frame_counter;
    volatile std::uint64_t heartbeat_ms;  // GetTickCount64() of the last publish (system-wide clock)
    volatile std::int64_t slot_seq[kSlotCount];  // seqlock per slot: odd = being written
};
static_assert(sizeof(SectionHeader) <= kHeaderSize);

inline std::uint8_t* slot_data(void* section, std::uint32_t slot) {
    return static_cast<std::uint8_t*>(section) + kHeaderSize + kFrameSize * slot;
}

}  // namespace djivcam::vcam
