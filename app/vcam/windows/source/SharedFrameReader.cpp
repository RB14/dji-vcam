#include "pch.h"
#include "SharedFrameReader.h"

#include <sddl.h>
#include <cstdarg>
#include <string>

#include "djivcam/vcam_protocol.h"

namespace vc = djivcam::vcam;

namespace
{
	constexpr BYTE kNoSignalLuma = 32;  // dark gray
	constexpr BYTE kNeutralChroma = 128;
	constexpr int kMaxLoggedFailures = 5;

	// Diagnostics: %ProgramData%\DJI VCam\logs\source-<pid>.log (the media source runs in the
	// Frame Server service and inside camera apps, so a debugger is rarely at hand).
	void Log(const wchar_t* format, ...)
	{
		wchar_t folder[MAX_PATH];
		if (!GetEnvironmentVariableW(L"ProgramData", folder, MAX_PATH))
			return;
		std::wstring path = std::wstring(folder) + L"\\DJI VCam\\logs";
		CreateDirectoryW(path.c_str(), nullptr);
		path += L"\\source-" + std::to_wstring(GetCurrentProcessId()) + L".log";
		wchar_t message[512];
		va_list args;
		va_start(args, format);
		_vsnwprintf_s(message, _TRUNCATE, format, args);
		va_end(args);
		SYSTEMTIME now;
		GetLocalTime(&now);
		wchar_t line[640];
		swprintf_s(line, L"%02u:%02u:%02u.%03u %s\r\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, message);
		auto file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return;
		auto utf8 = std::string(WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr), '\0');
		WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), (int)utf8.size(), nullptr, nullptr);
		DWORD written = 0;
		WriteFile(file, utf8.data(), (DWORD)strlen(utf8.c_str()), &written, nullptr);
		CloseHandle(file);
	}

	std::wstring ProcessName()
	{
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		return path;
	}
}

SharedFrameReader::~SharedFrameReader()
{
	if (_view)
		UnmapViewOfFile(_view);
	if (_section)
		CloseHandle(_section);
}

HRESULT SharedFrameReader::EnsureSection()
{
	if (_view)
		return S_OK;

	static int failures = 0;
	DWORD session = 0;
	ProcessIdToSessionId(GetCurrentProcessId(), &session);

	if (session != 0)
	{
		// Loaded inside a camera app: the DJI VCam app shares frames in the session namespace.
		_section = OpenFileMappingW(FILE_MAP_READ, FALSE, vc::kLocalSectionName);
	}
	if (!_section)
	{
		// Inside the Frame Server (session 0) creating a Global\ section is allowed; the DACL lets
		// the app in the user session open it for writing.
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		RETURN_IF_WIN32_BOOL_FALSE(ConvertStringSecurityDescriptorToSecurityDescriptorW(vc::kSectionSddl, SDDL_REVISION_1, &descriptor, nullptr));
		SECURITY_ATTRIBUTES attributes{ sizeof(attributes), descriptor, FALSE };
		_section = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, (DWORD)vc::kSectionSize, vc::kSectionName);
		auto createError = GetLastError();
		LocalFree(descriptor);
		if (!_section)
			_section = OpenFileMappingW(FILE_MAP_READ, FALSE, vc::kSectionName);
		if (!_section && failures++ < kMaxLoggedFailures)
			Log(L"no section yet: session %u, create Global error %u, open error %u (%s)", session, createError, GetLastError(), ProcessName().c_str());
	}
	RETURN_LAST_ERROR_IF_NULL(_section);
	_view = MapViewOfFile(_section, FILE_MAP_READ, 0, 0, vc::kSectionSize);
	RETURN_LAST_ERROR_IF_NULL(_view);
	Log(L"section mapped: session %u (%s)", session, ProcessName().c_str());
	return S_OK;
}

bool SharedFrameReader::CopyLatest(BYTE* y, LONG pitch)
{
	if (!_view)
		return false;

	auto header = static_cast<const vc::SectionHeader*>(_view);
	auto fresh = header->magic == vc::kMagic && header->version == vc::kVersion &&
		header->width == vc::kWidth && header->height == vc::kHeight &&
		GetTickCount64() - header->heartbeat_ms <= vc::kStaleAfterMs;
	if (!fresh)
		return false;

	auto slot = header->latest_slot;
	if (slot >= 0 && slot < (LONG64)vc::kSlotCount)
	{
		// seqlock read: the sequence must be even and unchanged across the copy
		auto before = header->slot_seq[slot];
		MemoryBarrier();
		if (!(before & 1))
		{
			_last.resize(vc::kFrameSize);
			memcpy(_last.data(), vc::slot_data(_view, (uint32_t)slot), vc::kFrameSize);
			MemoryBarrier();
			if (header->slot_seq[slot] != before)
				_last.clear();  // torn read: fall back below
		}
	}
	if (_last.size() != vc::kFrameSize)
		return false;

	// NV12: kHeight rows of luma, then kHeight / 2 rows of interleaved chroma, same pitch
	for (UINT row = 0; row < vc::kHeight * 3 / 2; row++)
	{
		memcpy(y + (size_t)row * pitch, _last.data() + (size_t)row * vc::kWidth, vc::kWidth);
	}
	return true;
}

void SharedFrameReader::FillNoSignal(BYTE* y, LONG pitch)
{
	for (UINT row = 0; row < vc::kHeight; row++)
		memset(y + (size_t)row * pitch, kNoSignalLuma, vc::kWidth);
	for (UINT row = vc::kHeight; row < vc::kHeight * 3 / 2; row++)
		memset(y + (size_t)row * pitch, kNeutralChroma, vc::kWidth);
}

HRESULT SharedFrameReader::Fill(IMFSample* sample)
{
	RETURN_HR_IF_NULL(E_POINTER, sample);
	if (!_view)
		LOG_IF_FAILED(EnsureSection());  // retried on every frame until it works

	wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
	RETURN_IF_FAILED(sample->GetBufferByIndex(0, &buffer));

	wil::com_ptr_nothrow<IMF2DBuffer2> buffer2d;
	if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d))))
	{
		BYTE* scanline0 = nullptr;
		LONG pitch = 0;
		BYTE* start = nullptr;
		DWORD length = 0;
		RETURN_IF_FAILED(buffer2d->Lock2DSize(MF2DBuffer_LockFlags_Write, &scanline0, &pitch, &start, &length));
		if (!CopyLatest(scanline0, pitch))
			FillNoSignal(scanline0, pitch);
		RETURN_IF_FAILED(buffer2d->Unlock2D());
		return S_OK;
	}

	BYTE* data = nullptr;
	DWORD max = 0;
	RETURN_IF_FAILED(buffer->Lock(&data, &max, nullptr));
	if (max >= vc::kFrameSize && !CopyLatest(data, vc::kWidth))
		FillNoSignal(data, vc::kWidth);
	RETURN_IF_FAILED(buffer->Unlock());
	RETURN_IF_FAILED(buffer->SetCurrentLength((DWORD)vc::kFrameSize));
	return S_OK;
}
