#pragma once

#include <cstdint>
#include <vector>

#include "galaxylib.h"

namespace app::events {

// One galaxy's per-point orbit data plus per-galaxy shader params.
struct CloudBuffers {
    std::vector<float> orbitA;
    std::vector<float> orbitB;
    std::vector<float> theta0;     // degrees
    std::vector<float> velTheta;   // degrees per time unit
    std::vector<float> tiltAngle;  // radians
    std::vector<float> colors;     // rgb per point
    std::vector<float> mags;
    std::vector<std::uint8_t> kinds;  // GalaxylibKind, kind-contiguous
    GalaxylibGalaxyParams params{};
};

}  // namespace app::events
