// DJI camera models, as their Bluetooth advertisement names them: the byte right after DJI's
// company ID in the manufacturer data (docs/protocol-notes.md a.1).
#pragma once

#include <cstdint>
#include <string>

namespace djivcam::camera {

inline constexpr std::uint8_t kModelAction5Pro = 0x15;

// "Osmo Action 5 Pro", or empty for a model byte not in the table.
std::string model_name(std::uint8_t model);

// Whether DJI VCam has been tested with this model: so far only the Osmo Action 5 Pro.
bool model_tested(std::uint8_t model);

}  // namespace djivcam::camera
