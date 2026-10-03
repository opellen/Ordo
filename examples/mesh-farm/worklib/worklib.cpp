// Implementation behind worklib.h: plain C++, no Qt/ordo/GL. Geometry math
// runs in double precision internally, cast to float only on output.
#include "worklib.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Small math helpers
// ---------------------------------------------------------------------------

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }

double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double length(const Vec3& a) { return std::sqrt(dot(a, a)); }

Vec3 normalized(const Vec3& a) {
    const double len = length(a);
    return len > 1e-20 ? a * (1.0 / len) : Vec3{0.0, 1.0, 0.0};
}

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// splitmix64: the only randomness source, chained off the caller's seed so
// same-seed-in gives same-bytes-out regardless of <random>'s engines.
// ---------------------------------------------------------------------------

struct SplitMix64 {
    uint64_t state;
    explicit SplitMix64(uint64_t seed) : state(seed) {}

    uint64_t next() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Uniform double in [0, 1).
    double nextUnit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

    double range(double lo, double hi) { return lo + (hi - lo) * nextUnit(); }
};

// One splitmix64 finishing round used as a hash, to seed a per-vertex RNG
// from (seed, vertexIndex).
uint64_t mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

uint64_t vertexSeed(uint64_t seed, uint32_t vertexIndex) {
    return mix64(seed + static_cast<uint64_t>(vertexIndex) * 0x9E3779B97F4A7C15ULL);
}

// ---------------------------------------------------------------------------
// Raw mesh -- positions plus a triangle list. Both part families build one;
// everything after (winding, smoothing, normals, AO) is family-agnostic.
// ---------------------------------------------------------------------------

struct RawMesh {
    std::vector<Vec3> positions;
    std::vector<uint32_t> indices;  // 3 per triangle
};

// Triangulates a "ringed" surface: `ringCount` rings of `segCount` vertices
// (ring-major, ring 0 = bottom, last = top) plus bottom/top apexes already
// in `positions`. Shared by lathe and twisted-extrude (same tube-with-caps
// topology). Winding follows a plain cylinder; ensureOutwardWinding() is
// insurance on top, not a substitute.
void triangulateRinged(int ringCount, int segCount, uint32_t bottomApex, uint32_t topApex,
                        std::vector<uint32_t>& indices) {
    auto at = [segCount](int ring, int seg) -> uint32_t {
        return static_cast<uint32_t>(ring * segCount + seg);
    };

    for (int ring = 0; ring + 1 < ringCount; ++ring) {
        for (int seg = 0; seg < segCount; ++seg) {
            const int segNext = (seg + 1) % segCount;
            const uint32_t v00 = at(ring, seg);
            const uint32_t v01 = at(ring, segNext);
            const uint32_t v10 = at(ring + 1, seg);
            const uint32_t v11 = at(ring + 1, segNext);
            indices.insert(indices.end(), {v00, v10, v11});
            indices.insert(indices.end(), {v00, v11, v01});
        }
    }

    for (int seg = 0; seg < segCount; ++seg) {
        const int segNext = (seg + 1) % segCount;
        indices.insert(indices.end(), {bottomApex, at(0, seg), at(0, segNext)});
        indices.insert(indices.end(), {topApex, at(ringCount - 1, segNext), at(ringCount - 1, seg)});
    }
}

// Flips every triangle's winding if the mesh's signed volume is negative
// (positive signed volume means outward-facing winding).
void ensureOutwardWinding(const std::vector<Vec3>& positions, std::vector<uint32_t>& indices) {
    double signedVolumeTimes6 = 0.0;
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const Vec3& v0 = positions[indices[i]];
        const Vec3& v1 = positions[indices[i + 1]];
        const Vec3& v2 = positions[indices[i + 2]];
        signedVolumeTimes6 += dot(v0, cross(v1, v2));
    }
    if (signedVolumeTimes6 < 0.0) {
        for (std::size_t i = 0; i < indices.size(); i += 3) {
            std::swap(indices[i + 1], indices[i + 2]);
        }
    }
}

// ---------------------------------------------------------------------------
// Family 1: LATHE -- a random radius profile, smoothed, revolved around Y.
// ---------------------------------------------------------------------------

RawMesh buildLathePart(SplitMix64& rng) {
    // Random control radii, cosine-interpolated (not splined) into a denser
    // profile -- cosine interpolation never overshoots its two neighbors,
    // so the revolved radius can't go negative or self-intersect.
    const int controlCount = 6 + static_cast<int>(rng.next() % 5);  // 6..10
    std::vector<double> controlRadii(controlCount);
    for (double& r : controlRadii) {
        r = rng.range(0.25, 1.0);
    }

    constexpr int kProfileSamples = 32;
    constexpr double kHalfHeight = 1.0;
    std::vector<double> profileRadius(kProfileSamples);
    std::vector<double> profileY(kProfileSamples);
    for (int s = 0; s < kProfileSamples; ++s) {
        const double t = static_cast<double>(s) / (kProfileSamples - 1);
        profileY[s] = -kHalfHeight + t * (2.0 * kHalfHeight);

        const double cp = t * (controlCount - 1);
        const int i0 = std::min(static_cast<int>(cp), controlCount - 2 >= 0 ? controlCount - 2 : 0);
        const int i1 = std::min(i0 + 1, controlCount - 1);
        const double frac = cp - i0;
        const double eased = (1.0 - std::cos(frac * kPi)) * 0.5;  // cosine interpolation
        profileRadius[s] = controlRadii[i0] * (1.0 - eased) + controlRadii[i1] * eased;
    }

    // Revolve resolution, kept narrow: AO below is O(V^2 * rays), so a
    // wider band would push a bake from seconds into several seconds.
    const int segments = 20 + static_cast<int>(rng.next() % 9);  // 20..28

    RawMesh mesh;
    mesh.positions.reserve(static_cast<std::size_t>(kProfileSamples) * segments + 2);
    for (int s = 0; s < kProfileSamples; ++s) {
        for (int seg = 0; seg < segments; ++seg) {
            const double angle = seg * (2.0 * kPi / segments);
            mesh.positions.push_back({profileRadius[s] * std::cos(angle), profileY[s],
                                       profileRadius[s] * std::sin(angle)});
        }
    }
    const auto bottomApex = static_cast<uint32_t>(mesh.positions.size());
    mesh.positions.push_back({0.0, profileY.front(), 0.0});
    const auto topApex = static_cast<uint32_t>(mesh.positions.size());
    mesh.positions.push_back({0.0, profileY.back(), 0.0});

    mesh.indices.reserve((static_cast<std::size_t>(kProfileSamples) - 1) * segments * 6 +
                          static_cast<std::size_t>(segments) * 6);
    triangulateRinged(kProfileSamples, segments, bottomApex, topApex, mesh.indices);
    return mesh;
}

// ---------------------------------------------------------------------------
// Family 2: TWISTED EXTRUDE -- jittered polygon extruded in layers, each
// rotated further about Y than the last.
// ---------------------------------------------------------------------------

RawMesh buildTwistedExtrudePart(SplitMix64& rng) {
    const int cornerCount = 5 + static_cast<int>(rng.next() % 5);  // 5..9

    // Jitter never exceeds a slice of the sector, so angles stay strictly
    // increasing and the polygon stays star-shaped about Y -- required for
    // fan triangulation to stay watertight.
    const double sector = 2.0 * kPi / cornerCount;
    std::vector<double> cornerAngle(cornerCount);
    std::vector<double> cornerRadius(cornerCount);
    for (int c = 0; c < cornerCount; ++c) {
        cornerAngle[c] = c * sector + rng.range(-0.15 * sector, 0.15 * sector);
        cornerRadius[c] = rng.range(0.4, 0.85);
    }

    const int layerCount = 8 + static_cast<int>(rng.next() % 9);          // 8..16
    const double totalTwist = rng.range(0.0, kPi / 2.0);                  // up to ~90 degrees
    constexpr double kHeight = 1.5;
    const int ringCount = layerCount + 1;

    RawMesh mesh;
    mesh.positions.reserve(static_cast<std::size_t>(ringCount) * cornerCount + 2);
    for (int ring = 0; ring < ringCount; ++ring) {
        const double t = static_cast<double>(ring) / layerCount;
        const double y = -kHeight * 0.5 + t * kHeight;
        const double twist = t * totalTwist;
        for (int c = 0; c < cornerCount; ++c) {
            const double angle = cornerAngle[c] + twist;
            mesh.positions.push_back(
                {cornerRadius[c] * std::cos(angle), y, cornerRadius[c] * std::sin(angle)});
        }
    }
    const auto bottomApex = static_cast<uint32_t>(mesh.positions.size());
    mesh.positions.push_back({0.0, -kHeight * 0.5, 0.0});
    const auto topApex = static_cast<uint32_t>(mesh.positions.size());
    mesh.positions.push_back({0.0, kHeight * 0.5, 0.0});

    mesh.indices.reserve(static_cast<std::size_t>(layerCount) * cornerCount * 6 +
                          static_cast<std::size_t>(cornerCount) * 6);
    triangulateRinged(ringCount, cornerCount, bottomApex, topApex, mesh.indices);
    return mesh;
}

// ---------------------------------------------------------------------------
// Uniform Laplacian smoothing
// ---------------------------------------------------------------------------

std::vector<std::vector<uint32_t>> buildAdjacency(const std::vector<uint32_t>& indices,
                                                    std::size_t vertexCount) {
    std::vector<std::vector<uint32_t>> adjacency(vertexCount);
    auto addEdge = [&adjacency](uint32_t a, uint32_t b) { adjacency[a].push_back(b); };
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        addEdge(a, b); addEdge(b, a);
        addEdge(b, c); addEdge(c, b);
        addEdge(c, a); addEdge(a, c);
    }
    for (auto& neighbors : adjacency) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    return adjacency;
}

// lambda = 0.5, positions only, `iterations` full passes. Each pass reads
// last iteration's positions and writes a fresh buffer so a vertex never
// sees a neighbor that has already moved this pass.
void laplacianSmooth(std::vector<Vec3>& positions,
                      const std::vector<std::vector<uint32_t>>& adjacency, uint32_t iterations) {
    if (iterations == 0) {
        return;
    }
    std::vector<Vec3> next = positions;
    for (uint32_t iter = 0; iter < iterations; ++iter) {
        for (std::size_t v = 0; v < positions.size(); ++v) {
            const auto& neighbors = adjacency[v];
            if (neighbors.empty()) {
                next[v] = positions[v];
                continue;
            }
            Vec3 avg{0.0, 0.0, 0.0};
            for (uint32_t n : neighbors) {
                avg = avg + positions[n];
            }
            avg = avg * (1.0 / static_cast<double>(neighbors.size()));
            next[v] = positions[v] * 0.5 + avg * 0.5;  // lambda = 0.5
        }
        std::swap(positions, next);
    }
}

// Area-weighted vertex normals: each triangle contributes its unnormalized
// cross-product normal (twice its area) to its three vertices, so large
// triangles outvote slivers before the final per-vertex normalize.
std::vector<Vec3> computeAreaWeightedNormals(const std::vector<Vec3>& positions,
                                              const std::vector<uint32_t>& indices) {
    std::vector<Vec3> normals(positions.size(), Vec3{0.0, 0.0, 0.0});
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
        const Vec3 faceNormal = cross(positions[ib] - positions[ia], positions[ic] - positions[ia]);
        normals[ia] = normals[ia] + faceNormal;
        normals[ib] = normals[ib] + faceNormal;
        normals[ic] = normals[ic] + faceNormal;
    }
    for (Vec3& n : normals) {
        n = normalized(n);
    }
    return normals;
}

// ---------------------------------------------------------------------------
// Ambient occlusion bake -- deliberately no BVH: O(V * rays * T), the job's
// weight knob, tuned by aoRaysPerVertex alone.
// ---------------------------------------------------------------------------

// Branchless orthonormal basis about `normal` (Duff et al.). Turns a 2D
// hemisphere sample into a world-space ray direction.
void buildBasis(const Vec3& normal, Vec3& tangent, Vec3& bitangent) {
    const double sign = normal.z >= 0.0 ? 1.0 : -1.0;
    const double a = -1.0 / (sign + normal.z);
    const double b = normal.x * normal.y * a;
    tangent = {1.0 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x};
    bitangent = {b, sign + normal.y * normal.y * a, -normal.y};
}

Vec3 cosineWeightedHemisphereDir(const Vec3& normal, double u1, double u2) {
    const double r = std::sqrt(u1);
    const double theta = 2.0 * kPi * u2;
    const double localX = r * std::cos(theta);
    const double localY = r * std::sin(theta);
    const double localZ = std::sqrt(std::max(0.0, 1.0 - u1));

    Vec3 tangent, bitangent;
    buildBasis(normal, tangent, bitangent);
    return normalized(tangent * localX + bitangent * localY + normal * localZ);
}

// Moeller-Trumbore, any-hit (no closest-hit bookkeeping needed for AO).
bool rayHitsTriangle(const Vec3& origin, const Vec3& dir, const Vec3& v0, const Vec3& v1,
                      const Vec3& v2) {
    constexpr double kEpsilon = 1e-9;
    constexpr double kTMin = 1e-6;

    const Vec3 edge1 = v1 - v0;
    const Vec3 edge2 = v2 - v0;
    const Vec3 pvec = cross(dir, edge2);
    const double det = dot(edge1, pvec);
    if (std::fabs(det) < kEpsilon) {
        return false;  // ray parallel to the triangle's plane
    }
    const double invDet = 1.0 / det;

    const Vec3 tvec = origin - v0;
    const double u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) {
        return false;
    }

    const Vec3 qvec = cross(tvec, edge1);
    const double v = dot(dir, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) {
        return false;
    }

    const double t = dot(edge2, qvec) * invDet;
    return t > kTMin;
}

// Writes straight into `aoOut`, the same buffer `view.aoValues` points at,
// so a checkpoint callback sees real stored values mid-bake. Caller
// pre-fills aoOut with the -1.0 sentinel before this runs.
void bakeAmbientOcclusion(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals,
                           const std::vector<uint32_t>& indices, uint64_t seed, uint32_t raysPerVertex,
                           float* aoOut, const WorklibMesh& view, WorklibProgressFn progress, void* user) {
    constexpr double kOriginEpsilon = 1e-3;
    const auto vertexCount = static_cast<uint32_t>(positions.size());
    const uint32_t checkpointStride = std::max<uint32_t>(1, vertexCount / 10);

    for (std::size_t v = 0; v < positions.size(); ++v) {
        const Vec3 origin = positions[v] + normals[v] * kOriginEpsilon;
        SplitMix64 rng(vertexSeed(seed, static_cast<uint32_t>(v)));

        uint32_t unoccluded = 0;
        for (uint32_t r = 0; r < raysPerVertex; ++r) {
            const double u1 = rng.nextUnit();
            const double u2 = rng.nextUnit();
            const Vec3 dir = cosineWeightedHemisphereDir(normals[v], u1, u2);

            bool occluded = false;
            for (std::size_t i = 0; i < indices.size() && !occluded; i += 3) {
                occluded = rayHitsTriangle(origin, dir, positions[indices[i]],
                                            positions[indices[i + 1]], positions[indices[i + 2]]);
            }
            if (!occluded) {
                ++unoccluded;
            }
        }
        aoOut[v] = static_cast<float>(static_cast<double>(unoccluded) / static_cast<double>(raysPerVertex));

        // doneCount == vertexCount means finished; that result returns via
        // worklib_bake_part's return value, not a 1.0 callback.
        const auto doneCount = static_cast<uint32_t>(v) + 1;
        if (progress != nullptr && doneCount % checkpointStride == 0 && doneCount != vertexCount) {
            progress(&view, static_cast<float>(doneCount) / static_cast<float>(vertexCount), user);
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// C ABI
// ---------------------------------------------------------------------------

extern "C" {

int worklib_bake_part(uint64_t seed, uint32_t smoothIters, uint32_t aoRaysPerVertex,
                       WorklibProgressFn progress, void* user, WorklibMesh* out) {
    if (out == nullptr) {
        return 1;
    }
    if (aoRaysPerVertex < 1 || aoRaysPerVertex > 4096) {
        std::memset(out, 0, sizeof(*out));
        return 2;
    }
    if (smoothIters > 10000) {
        std::memset(out, 0, sizeof(*out));
        return 3;
    }

    SplitMix64 rng(seed);
    const bool isLathe = (rng.next() & 1) == 0;  // roughly 50/50, decided once per part
    RawMesh raw = isLathe ? buildLathePart(rng) : buildTwistedExtrudePart(rng);

    ensureOutwardWinding(raw.positions, raw.indices);

    const auto adjacency = buildAdjacency(raw.indices, raw.positions.size());
    laplacianSmooth(raw.positions, adjacency, smoothIters);

    const std::vector<Vec3> normals = computeAreaWeightedNormals(raw.positions, raw.indices);

    const auto vertexCount = static_cast<uint32_t>(raw.positions.size());
    const auto triangleCount = static_cast<uint32_t>(raw.indices.size() / 3);

    // Allocated right after geometry goes final: the first progress
    // callback (fraction 0.0) needs a real WorklibMesh, and the AO loop
    // writes results straight into aoOut as it runs.
    auto* positions = static_cast<float*>(std::malloc(sizeof(float) * 3 * vertexCount));
    auto* normalsOut = static_cast<float*>(std::malloc(sizeof(float) * 3 * vertexCount));
    auto* aoOut = static_cast<float*>(std::malloc(sizeof(float) * vertexCount));
    auto* indicesOut = static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * raw.indices.size()));
    if (!positions || !normalsOut || !aoOut || !indicesOut) {
        std::free(positions);
        std::free(normalsOut);
        std::free(aoOut);
        std::free(indicesOut);
        std::memset(out, 0, sizeof(*out));
        return 4;
    }

    for (uint32_t v = 0; v < vertexCount; ++v) {
        positions[v * 3 + 0] = static_cast<float>(raw.positions[v].x);
        positions[v * 3 + 1] = static_cast<float>(raw.positions[v].y);
        positions[v * 3 + 2] = static_cast<float>(raw.positions[v].z);
        normalsOut[v * 3 + 0] = static_cast<float>(normals[v].x);
        normalsOut[v * 3 + 1] = static_cast<float>(normals[v].y);
        normalsOut[v * 3 + 2] = static_cast<float>(normals[v].z);
        aoOut[v] = -1.0f;  // sentinel: this vertex's rays haven't been cast yet
    }
    std::memcpy(indicesOut, raw.indices.data(), sizeof(uint32_t) * raw.indices.size());

    WorklibMesh view{};
    view.positions = positions;
    view.normals = normalsOut;
    view.aoValues = aoOut;
    view.indices = indicesOut;
    view.vertexCount = vertexCount;
    view.triangleCount = triangleCount;

    if (progress != nullptr) {
        progress(&view, 0.0f, user);
    }

    bakeAmbientOcclusion(raw.positions, normals, raw.indices, seed, aoRaysPerVertex, aoOut, view, progress,
                          user);

    // `out`'s arrays ARE `view`'s arrays -- nothing is copied here.
    out->positions = positions;
    out->normals = normalsOut;
    out->aoValues = aoOut;
    out->indices = indicesOut;
    out->vertexCount = vertexCount;
    out->triangleCount = triangleCount;
    return 0;
}

void worklib_free_mesh(WorklibMesh* mesh) {
    if (mesh == nullptr) {
        return;
    }
    std::free(mesh->positions);
    std::free(mesh->normals);
    std::free(mesh->aoValues);
    std::free(mesh->indices);
    std::memset(mesh, 0, sizeof(*mesh));  // zeroed struct tolerates a repeat free
}

}  // extern "C"
