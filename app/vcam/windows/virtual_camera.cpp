#include "djivcam/virtual_camera.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <shellapi.h>

#include <functional>

#include <atomic>
#include <cstring>
#include <cwchar>
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

// Runs `work` on a fresh multithreaded-apartment thread with Media Foundation started (the
// virtual camera API must not run on an STA UI thread).
HRESULT run_in_mta(const std::function<HRESULT()>& work) {
    HRESULT result = E_FAIL;
    std::thread([&] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const HRESULT mf = MFStartup(MF_VERSION);
        result = SUCCEEDED(mf) ? work() : mf;
        if (SUCCEEDED(mf)) {
            MFShutdown();
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
    }).join();
    return result;
}

// The camera is keyed off these parameters: the same ones reopen the same camera (and its
// Windows settings, e.g. "Allow multiple apps").
HRESULT open_camera(bool all_users, IMFVirtualCamera** camera) {
    return MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_System,
                                 all_users ? MFVirtualCameraAccess_AllUsers : MFVirtualCameraAccess_CurrentUser,
                                 kFriendlyName, kSourceClsid, nullptr, 0, camera);
}

}  // namespace

struct VirtualCamera::Impl {
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

std::vector<std::wstring> VirtualCamera::list_cameras() {
    std::vector<std::wstring> names;
    run_in_mta([&names] {
        IMFAttributes* attributes = nullptr;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (SUCCEEDED(hr)) {
            hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        }
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        if (SUCCEEDED(hr)) {
            hr = MFEnumDeviceSources(attributes, &devices, &count);
        }
        for (UINT32 i = 0; i < count; ++i) {
            wchar_t* name = nullptr;
            UINT32 length = 0;
            if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length))) {
                names.emplace_back(name);
                CoTaskMemFree(name);
            }
            devices[i]->Release();
        }
        CoTaskMemFree(devices);
        if (attributes) {
            attributes->Release();
        }
        return hr;
    });
    return names;
}

bool VirtualCamera::camera_registered() {
    // Windows appends " (Windows Virtual Camera)" to the name we register.
    for (const std::wstring& name : list_cameras()) {
        if (name.rfind(kFriendlyName, 0) == 0) {
            return true;
        }
    }
    return false;
}

bool VirtualCamera::register_camera(bool all_users, std::string* error) {
    const HRESULT hr = run_in_mta([all_users] {
        IMFVirtualCamera* camera = nullptr;
        HRESULT result = open_camera(all_users, &camera);
        if (SUCCEEDED(result)) {
            result = camera->Start(nullptr);  // system lifetime: it stays registered after this process
            camera->Release();
        }
        return result;
    });
    if (FAILED(hr)) {
        *error = hresult_text("Registering the DJI VCam webcam", hr);
        return false;
    }
    return true;
}

bool VirtualCamera::unregister_camera(bool all_users, std::string* error) {
    const HRESULT hr = run_in_mta([all_users] {
        IMFVirtualCamera* camera = nullptr;
        HRESULT result = open_camera(all_users, &camera);
        if (SUCCEEDED(result)) {
            result = camera->Remove();
            camera->Release();
        }
        return result;
    });
    if (FAILED(hr)) {
        *error = hresult_text("Removing the DJI VCam webcam", hr);
        return false;
    }
    return true;
}

void VirtualCamera::start() { impl_->running = true; }

void VirtualCamera::stop() {
    impl_->running = false;
    std::lock_guard publishing(impl_->publish_mutex);
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
