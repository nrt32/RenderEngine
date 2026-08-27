#pragma once
#include <cstdint>
namespace re::scene {
enum class FieldId : uint8_t {
    Rect = 0,
    Plane = 1,
    CameraView = 2,
    CameraProj = 3,
    Items = 4,
    Transform = 5,
    Material = 6,
    TransferFunction = 7,
    ClearColor = 8,
    DepthTest = 9,
    Lights = 10,
};
}
