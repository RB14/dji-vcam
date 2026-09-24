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

private:
	bool CopyLatest(BYTE* y, LONG pitch);
	void FillNoSignal(BYTE* y, LONG pitch);

	HANDLE _section = nullptr;
	void* _view = nullptr;
	std::vector<BYTE> _last;  // last good frame, repeated while the writer is between frames
};
