// Supplies the virtual camera's NV12 frames from the section the DJI VCam app writes into
// (see djivcam/vcam_protocol.h), or a dark "no signal" frame while the app sends nothing.
#pragma once

class SharedFrameReader
{
public:
	SharedFrameReader() = default;
	~SharedFrameReader();
	SharedFrameReader(const SharedFrameReader&) = delete;
	SharedFrameReader& operator=(const SharedFrameReader&) = delete;

	// Creates (as the Frame Server service) or opens the shared section.
	HRESULT EnsureSection();
	// Fills `sample` (allocated by the frame server's allocator) with the newest frame.
	HRESULT Fill(IMFSample* sample);
	// Paces the samples to the app's frames: returns at once if the app has published a frame
	// since the last Fill(), else when it publishes the next one (so a sample is never a stale
	// repeat and waits no longer than the frame). Without new frames it returns after 100 ms
	// while the app runs (a stalled camera), or every 33 ms (30 fps) with no app ("no signal").
	void WaitForNextFrame();

private:
	bool CopyLatest(BYTE* y, LONG pitch);
	void FillNoSignal(BYTE* y, LONG pitch);

	HANDLE _section = nullptr;
	void* _view = nullptr;
	std::vector<BYTE> _last;  // last good frame, repeated while the writer is between frames
	std::vector<BYTE> _scratch;  // a frame being read; becomes _last if it was not torn
	bool _writable = false;  // we created the section (camera service): we write reader_heartbeat_ms
	LONG64 _delivered = -1;  // the app's frame_counter at the last Fill()
	std::chrono::steady_clock::time_point _lastFill{};
};
