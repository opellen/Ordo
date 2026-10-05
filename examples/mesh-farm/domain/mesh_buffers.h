#pragma once

#include <cstdint>
#include <vector>

namespace app::events {

// Full mesh payload for one baked part: positions/normals/ao are one entry
// per vertex component, indices is the triangle list.
struct MeshBuffers {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> ao;
    std::vector<std::uint32_t> indices;
};

}  // namespace app::events
