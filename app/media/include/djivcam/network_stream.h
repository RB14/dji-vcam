// Reads the H.264 video of a network stream (the RTMP feed, from the local RTMP server's RTSP
// output) as access units for the same decoder as the live view: each packet as soon as it arrives
// (no demuxer buffering), in Annex B, with the stream's parameter sets in front of every keyframe.
#pragma once

#include <functional>
#include <stop_token>
#include <string>

#include "djivcam/h264.h"

namespace djivcam::media {

class NetworkStream {
public:
    using OnUnit = std::function<void(h264::AccessUnit&&)>;

    // Opens `url` and delivers its video until `stop` is requested or the stream ends or fails.
    // Returns why it ended: empty when stopped.
    static std::string run(const std::string& url, const OnUnit& on_unit, std::stop_token stop);
};

}  // namespace djivcam::media
