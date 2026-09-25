#include "djivcam/camera_model.h"

#include <iterator>

namespace djivcam::camera {
namespace {

struct Model {
    std::uint8_t id;
    const char* name;
};

// Moblin's DjiDeviceModel.swift (eerimoq/moblin 1bb4902); 0x15 seen on our own camera.
constexpr Model kModels[] = {
    {0x10, "Osmo Action 2"},  {0x12, "Osmo Action 3"}, {0x14, "Osmo Action 4"}, {0x15, "Osmo Action 5 Pro"},
    {0x17, "Osmo 360"},       {0x18, "Osmo Action 6"}, {0x20, "Osmo Pocket 3"}, {0x21, "Osmo Pocket 4"},
};

}  // namespace

std::string model_name(std::uint8_t model) {
    for (const Model& known : kModels) {
        if (known.id == model) {
            return known.name;
        }
    }
    return {};
}

bool model_tested(std::uint8_t model) { return model == kModelAction5Pro; }

}  // namespace djivcam::camera
