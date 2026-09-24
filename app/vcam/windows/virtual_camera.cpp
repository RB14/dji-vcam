#include "djivcam/virtual_camera.h"

#include <windows.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <shellapi.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#include "djivcam/vcam_protocol.h"

namespace djivcam::vcam {
namespace {

// How often to look for the section the camera service creates when an app opens the webcam (it
// shows its gray "no signal" picture until the app has found it).
constexpr ULONGLONG kReopenIntervalMs = 250;
// No frame taken by an app for this long: the webcam is no longer in use.
constexpr ULONGLONG kInUseTimeoutMs = 2000;

std::string hresult_text(const char* what, HRESULT hr) {
    char text[96];
    std::snprintf(text, sizeof(text), "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    return text;
}

}  // namespace

struct VirtualCamera::Impl {
    // The IMFVirtualCamera lives on its own MTA thread (the UI thread is STA).
    std::thread owner;
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    bool stop_requested = false;
    HRESULT result = S_OK;
    std::string error;

    // Writer side: publish() runs on the decoder thread. Frames go to the session-local section
    // (read by media sources loaded inside camera apps) and, once the Frame Server's media source
    // has created it, to the Global section.
    struct Target {
        HANDLE section = nullptr;
        std::uint8_t* view = nullptr;
        std::uint32_t next_slot = 0;  // kept here, not trusted from the shared header
    };
    Target local;
    Target global;
    ULONGLONG last_open_attempt = 0;
    std::atomic<bool> section_open{false};
    std::atomic<bool> running{false};
    std::mutex publish_mutex;  // publish() on the decoder thread vs stop() on the UI thread

    ~Impl() { close_sections(); }

    static void close(Target& target) {
        if (target.view) {
            UnmapViewOfFile(target.view);
        }
        if (target.section) {
            CloseHandle(target.section);
        }
        target = {};
    }

    void close_sections() {
        close(local);
        close(global);
        section_open = false;
    }

    static bool map(Target& target, HANDLE section) {
        if (!section) {
            return false;
        }
        target.section = section;
        target.view = static_cast<std::uint8_t*>(MapViewOfFile(section, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kSectionSize));
        if (!target.view) {
            close(target);
            return false;
        }
        auto* header = reinterpret_cast<SectionHeader*>(target.view);
        if (header->magic != kMagic) {
            header->version = kVersion;
            header->width = kWidth;
            header->height = kHeight;
            header->latest_slot = -1;
            MemoryBarrier();
            header->magic = kMagic;
        }
        // A writer that died mid-frame leaves a slot odd ("being written"): make every slot even.
        for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
            if (header->slot_seq[slot] & 1) {
                header->slot_seq[slot] = header->slot_seq[slot] + 1;
            }
        }
        const std::int64_t latest = header->latest_slot;
        target.next_slot = latest >= 0 && latest < static_cast<std::int64_t>(kSlotCount)
                               ? static_cast<std::uint32_t>((latest + 1) % kSlotCount)
                               : 0;
        return true;
    }

    bool reader_active() {
        std::lock_guard publishing(publish_mutex);
        if (!global.view) {
            return false;
        }
        const std::uint64_t beat = reinterpret_cast<const SectionHeader*>(global.view)->reader_heartbeat_ms;
        return beat == 0 || GetTickCount64() - beat < kInUseTimeoutMs;  // 0: an older media source
    }

    void ensure_sections() {
        if (!local.view) {
            map(local, CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                          static_cast<DWORD>(kSectionSize), kLocalSectionName));
        }
        const ULONGLONG now = GetTickCount64();
        if (!global.view && now - last_open_attempt >= kReopenIntervalMs) {
            last_open_attempt = now;
            map(global, OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, kSectionName));
        }
        section_open = global.view != nullptr;
    }

    static void write(Target& target, const std::uint8_t* nv12) {
        if (!target.view) {
            return;
        }
        auto* header = reinterpret_cast<SectionHeader*>(target.view);
        const std::uint32_t slot = target.next_slot;
        target.next_slot = (slot + 1) % kSlotCount;
        const std::int64_t writing = header->slot_seq[slot] | 1;  // odd: being written
        header->slot_seq[slot] = writing;
        MemoryBarrier();
        std::memcpy(slot_data(target.view, slot), nv12, kFrameSize);
        MemoryBarrier();
        header->slot_seq[slot] = writing + 1;  // even: complete
        header->latest_slot = slot;
        header->frame_counter = header->frame_counter + 1;
        header->heartbeat_ms = GetTickCount64();
    }

    void run() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool com = SUCCEEDED(hr);
        hr = MFStartup(MF_VERSION);
        const bool mf = SUCCEEDED(hr);
        IMFVirtualCamera* camera = nullptr;
        if (mf) {
            hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_Session,
                                       MFVirtualCameraAccess_CurrentUser, kFriendlyName, kSourceClsid, nullptr, 0,
                                       &camera);
            if (SUCCEEDED(hr)) {
                hr = camera->Start(nullptr);
            }
        }
        {
            std::lock_guard lock(mutex);
            result = hr;
            error = !mf ? hresult_text("MFStartup", hr)
                        : FAILED(hr) ? hresult_text("Registering the virtual camera", hr) : std::string();
            started = true;
        }
        changed.notify_all();
        if (SUCCEEDED(hr)) {
            std::unique_lock lock(mutex);
            changed.wait(lock, [this] { return stop_requested; });
        }
        if (camera) {
            camera->Remove();  // not Shutdown(): that would shut the media source down twice
            camera->Release();
        }
        if (mf) {
            MFShutdown();
        }
        if (com) {
            CoUninitialize();
        }
    }
};

VirtualCamera::VirtualCamera() : impl_(std::make_unique<Impl>()) {}

VirtualCamera::~VirtualCamera() { stop(); }

bool VirtualCamera::source_registered() {
    const std::wstring key = std::wstring(L"Software\\Classes\\CLSID\\") + kSourceClsid + L"\\InprocServer32";
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(handle);
    return true;
}

bool VirtualCamera::install_source(const std::wstring& source_dll, std::string* error) {
    wchar_t program_data[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"ProgramData", program_data, MAX_PATH)) {
        *error = "ProgramData folder not found";
        return false;
    }
    const std::wstring folder = std::wstring(program_data) + L"\\DJI VCam";
    const std::wstring target = folder + L"\\djivcam-source.dll";
    // One elevated command: create the folder, copy the DLL, register it (HKLM).
    const std::wstring parameters = L"/c mkdir \"" + folder + L"\" 2>nul & copy /y \"" + source_dll + L"\" \"" + target +
                                     L"\" && regsvr32 /s \"" + target + L"\"";
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = L"cmd.exe";
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        *error = "the administrator prompt was declined";
        return false;
    }
    WaitForSingleObject(info.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    if (code != 0 || !source_registered()) {
        *error = "registering the camera component failed";
        return false;
    }
    return true;
}

bool VirtualCamera::start(std::string* error) {
    stop();
    if (!source_registered()) {
        *error = "the DJI VCam camera component is not installed";
        return false;
    }
    impl_->started = false;
    impl_->stop_requested = false;
    impl_->owner = std::thread([this] { impl_->run(); });
    std::unique_lock lock(impl_->mutex);
    impl_->changed.wait(lock, [this] { return impl_->started; });
    if (FAILED(impl_->result)) {
        *error = impl_->error;
        lock.unlock();
        impl_->owner.join();
        return false;
    }
    impl_->running = true;
    return true;
}

void VirtualCamera::stop() {
    impl_->running = false;
    std::lock_guard publishing(impl_->publish_mutex);
    if (impl_->owner.joinable()) {
        {
            std::lock_guard lock(impl_->mutex);
            impl_->stop_requested = true;
        }
        impl_->changed.notify_all();
        impl_->owner.join();
    }
    impl_->close_sections();
}

bool VirtualCamera::running() const { return impl_->running; }

bool VirtualCamera::in_use() const {
    // The camera service's media source stamps the header each time it hands a frame to an app.
    return impl_->section_open && impl_->reader_active();
}

void VirtualCamera::publish(const std::uint8_t* nv12) {
    std::lock_guard publishing(impl_->publish_mutex);
    if (!running()) {
        return;
    }
    impl_->ensure_sections();
    Impl::write(impl_->local, nv12);
    Impl::write(impl_->global, nv12);
}

}  // namespace djivcam::vcam
