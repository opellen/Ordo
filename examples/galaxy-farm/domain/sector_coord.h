#pragma once

namespace app::events {

// Integer sector address.
struct SectorCoord {
    int x = 0;
    int y = 0;
};

inline bool operator==(const SectorCoord& a, const SectorCoord& b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(const SectorCoord& a, const SectorCoord& b) { return !(a == b); }

}  // namespace app::events
