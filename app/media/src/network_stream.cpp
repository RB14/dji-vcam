#include "djivcam/network_stream.h"

#include <cerrno>
#include <cstring>

extern "C" {
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

namespace djivcam::media {
namespace {

// Why the stream is not playing, in the words the app shows.
std::string error_text(int code) {
    if (code == AVERROR_HTTP_NOT_FOUND) {
        return "the camera is not streaming yet";
    }
    if (code == AVERROR(ETIMEDOUT)) {
        return "no video for 5 s";
    }
    if (code == AVERROR(ECONNREFUSED)) {
        return "the RTMP server is not running";
    }
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return text;
}

// Whether an Annex-B unit carries a sequence parameter set (NAL type 7).
bool has_sps(const std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i + 3 < size; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1 && (data[i + 3] & 0x1F) == 7) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::string NetworkStream::run(const std::string& url, const OnUnit& on_unit, std::stop_token stop) {
    static const bool network_ready = [] { return avformat_network_init() == 0; }();
    (void)network_ready;

    AVFormatContext* input = avformat_alloc_context();
    // Makes blocking reads give up as soon as a stop is requested.
    input->interrupt_callback.callback = [](void* opaque) {
        return static_cast<std::stop_token*>(opaque)->stop_requested() ? 1 : 0;
    };
    input->interrupt_callback.opaque = &stop;
    input->flags |= AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);  // every packet, in order (accuracy)
    av_dict_set(&options, "timeout", "5000000", 0);     // µs without data before giving up
    av_dict_set(&options, "fflags", "nobuffer", 0);
    const int opened = avformat_open_input(&input, url.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (opened < 0) {
        return stop.stop_requested() ? std::string() : error_text(opened);
    }

    std::string result;
    const int video = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video < 0 || input->streams[video]->codecpar->codec_id != AV_CODEC_ID_H264) {
        avformat_close_input(&input);
        return "no H.264 video in " + url;
    }
    // RTMP-style streams (length-prefixed NAL units, "AVCC") become Annex B; RTSP already is.
    const AVCodecParameters* codec = input->streams[video]->codecpar;
    AVBSFContext* annexb = nullptr;
    if (codec->extradata_size > 0 && codec->extradata[0] == 1) {
        const AVBitStreamFilter* filter = av_bsf_get_by_name("h264_mp4toannexb");
        if (filter && av_bsf_alloc(filter, &annexb) == 0) {
            avcodec_parameters_copy(annexb->par_in, codec);
            annexb->time_base_in = input->streams[video]->time_base;
            if (av_bsf_init(annexb) < 0) {
                av_bsf_free(&annexb);
            }
        }
    }
    // The parameter sets the SDP (or the filter) carries, for keyframes that lack them.
    const AVCodecParameters* described = annexb ? annexb->par_out : codec;
    const h264::Bytes parameter_sets(described->extradata, described->extradata + described->extradata_size);

    AVPacket* packet = av_packet_alloc();
    auto deliver = [&](const AVPacket& unit) {
        h264::AccessUnit out;
        out.keyframe = (unit.flags & AV_PKT_FLAG_KEY) != 0;
        if (out.keyframe && !has_sps(unit.data, static_cast<std::size_t>(unit.size)) && !parameter_sets.empty() &&
            parameter_sets[0] == 0) {
            out.data = parameter_sets;
        }
        out.data.insert(out.data.end(), unit.data, unit.data + unit.size);
        on_unit(std::move(out));
    };
    while (!stop.stop_requested()) {
        const int read = av_read_frame(input, packet);
        if (read < 0) {
            result = stop.stop_requested() ? std::string() : (read == AVERROR_EOF ? "the stream ended" : error_text(read));
            break;
        }
        if (packet->stream_index == video) {
            if (annexb) {
                if (av_bsf_send_packet(annexb, packet) == 0) {
                    while (av_bsf_receive_packet(annexb, packet) == 0) {
                        deliver(*packet);
                        av_packet_unref(packet);
                    }
                }
            } else {
                deliver(*packet);
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    av_bsf_free(&annexb);
    avformat_close_input(&input);
    return result;
}

}  // namespace djivcam::media
