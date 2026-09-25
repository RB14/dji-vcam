#include "djivcam/camera_model.h"

#include <gtest/gtest.h>

using djivcam::camera::model_name;
using djivcam::camera::model_tested;

TEST(CameraModel, NamesKnownModels) {
    EXPECT_EQ(model_name(0x15), "Osmo Action 5 Pro");
    EXPECT_EQ(model_name(0x18), "Osmo Action 6");
    EXPECT_EQ(model_name(0x20), "Osmo Pocket 3");
}

TEST(CameraModel, UnknownModelHasNoName) {
    EXPECT_EQ(model_name(0x00), "");
    EXPECT_EQ(model_name(0xFF), "");
}

TEST(CameraModel, OnlyTheAction5ProIsTested) {
    EXPECT_TRUE(model_tested(0x15));
    EXPECT_FALSE(model_tested(0x18));
    EXPECT_FALSE(model_tested(0x00));
}
