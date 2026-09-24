// osmolink-cli: runs a live-view session without the GUI and prints its progress.
//
// Usage: osmolink-cli [--seconds N] [--identifier-file PATH] [--dump PATH]
//   --identifier-file  file holding the approved pairing identifier (not printed)
//   --dump             write the received H.264 stream to PATH
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "osmolink/session.h"

int main(int argc, char* argv[]) {
    using namespace std::chrono;
    int seconds = 20;
    std::string identifier_file;
    std::string dump_path;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string flag = argv[i];
        if (flag == "--seconds") {
            seconds = std::stoi(argv[i + 1]);
        } else if (flag == "--identifier-file") {
            identifier_file = argv[i + 1];
        } else if (flag == "--dump") {
            dump_path = argv[i + 1];
        }
    }

    osmolink::SessionConfig config;
    if (!identifier_file.empty()) {
        std::ifstream in(identifier_file);
        std::getline(in, config.identifier);
    }
    std::ofstream dump;
    if (!dump_path.empty()) {
        dump.open(dump_path, std::ios::binary);
    }

    const auto start = steady_clock::now();
    auto stamp = [&] { return duration_cast<milliseconds>(steady_clock::now() - start).count() / 1000.0; };
    osmolink::LiveViewSession session(
        config,
        [&](std::span<const std::uint8_t> video) {
            if (dump) {
                dump.write(reinterpret_cast<const char*>(video.data()), static_cast<std::streamsize>(video.size()));
            }
        },
        [&](osmolink::SessionState state, const std::string& detail) {
            std::printf("[%7.3f] state: %s%s%s\n", stamp(), osmolink::to_string(state), detail.empty() ? "" : " - ",
                        detail.c_str());
            std::fflush(stdout);
        });
    session.start();
    osmolink::SessionStats previous{};
    for (int second = 0; second < seconds; ++second) {
        std::this_thread::sleep_for(1s);
        const auto stats = session.stats();
        std::printf("[%7.3f] datagrams +%llu, video +%llu (%.0f kbit/s), lost %llu, recovered %llu, dup %llu, reconnects %llu\n",
                    stamp(), static_cast<unsigned long long>(stats.datagrams - previous.datagrams),
                    static_cast<unsigned long long>(stats.video_datagrams - previous.video_datagrams),
                    static_cast<double>(stats.video_bytes - previous.video_bytes) * 8 / 1000,
                    static_cast<unsigned long long>(stats.lost), static_cast<unsigned long long>(stats.recovered),
                    static_cast<unsigned long long>(stats.duplicates), static_cast<unsigned long long>(stats.reconnects));
        std::fflush(stdout);
        previous = stats;
    }
    session.stop();
    return 0;
}
