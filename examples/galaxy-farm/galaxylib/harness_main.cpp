// Headless harness for the galaxylib C ABI. One named check per property;
// nonzero exit on any failure.
#include "galaxylib.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void pass(const char* name) { std::printf("PASS: %s\n", name); }

// ---------------------------------------------------------------------------
// Small shared helpers
// ---------------------------------------------------------------------------

// Per-kind point counts, scanned from `kinds` (never assumed from layout).
struct KindCounts {
    uint32_t haze = 0, star = 0, dust = 0, filament = 0, h2 = 0, h2Core = 0;
};

KindCounts countKinds(const GalaxylibCloud& cloud) {
    KindCounts c;
    for (uint32_t i = 0; i < cloud.starCount; ++i) {
        switch (cloud.kinds[i]) {
            case GALAXYLIB_KIND_HAZE: ++c.haze; break;
            case GALAXYLIB_KIND_STAR: ++c.star; break;
            case GALAXYLIB_KIND_DUST: ++c.dust; break;
            case GALAXYLIB_KIND_FILAMENT: ++c.filament; break;
            case GALAXYLIB_KIND_H2: ++c.h2; break;
            case GALAXYLIB_KIND_H2_CORE: ++c.h2Core; break;
            default: break;
        }
    }
    return c;
}

// Per-kind counts as a function of (type, starCount), independent of preset.
struct ExpectedCounts {
    uint32_t star = 0;
    uint32_t dust = 0;
    uint32_t h2 = 0;             // == h2Core always (halo/core pairs)
    uint32_t filamentSeeds = 0;  // seed count -- exact filament point total is seed-dependent
};

ExpectedCounts expectedCounts(uint32_t type, uint32_t starCount) {
    ExpectedCounts e;
    e.star = starCount;
    double dustMul = 1.0;
    double h2Mul = 1.0;
    if (type == GALAXYLIB_TYPE_ELLIPTICAL) {
        dustMul = 0.0;
        h2Mul = 0.0;
    } else if (type == GALAXYLIB_TYPE_STARBURST) {
        h2Mul = 3.0;
    } else if (type == GALAXYLIB_TYPE_DUST_BELT_LENTICULAR) {
        dustMul = 0.3;
    }
    e.dust = static_cast<uint32_t>(std::llround(static_cast<double>(starCount) * dustMul));
    const auto h2Base = static_cast<uint32_t>(std::llround(static_cast<double>(starCount) / 100.0));
    e.h2 = static_cast<uint32_t>(std::llround(static_cast<double>(h2Base) * h2Mul));
    e.filamentSeeds = e.dust / 100u;
    return e;
}

// ---------------------------------------------------------------------------
// 1. Determinism: same inputs -> identical bytes; different seed -> different bytes.
// ---------------------------------------------------------------------------

void checkDeterminism() {
    GalaxylibCloud a{};
    GalaxylibCloud b{};
    GalaxylibCloud c{};
    const int rcA = galaxylib_generate_galaxy(12345ULL, 0, 2000, nullptr, nullptr, &a);
    const int rcB = galaxylib_generate_galaxy(12345ULL, 0, 2000, nullptr, nullptr, &b);
    const int rcC = galaxylib_generate_galaxy(54321ULL, 0, 2000, nullptr, nullptr, &c);

    bool ok = true;
    if (rcA != 0 || rcB != 0 || rcC != 0) {
        std::fprintf(stderr, "FAIL: determinism: rc=%d/%d/%d\n", rcA, rcB,
                     rcC);
        ok = false;
    } else {
        const bool sameSeedIdentical =
            a.starCount == b.starCount &&
            std::memcmp(a.orbitA, b.orbitA, sizeof(float) * a.starCount) == 0 &&
            std::memcmp(a.orbitB, b.orbitB, sizeof(float) * a.starCount) == 0 &&
            std::memcmp(a.theta0, b.theta0, sizeof(float) * a.starCount) == 0 &&
            std::memcmp(a.velTheta, b.velTheta, sizeof(float) * a.starCount) == 0 &&
            std::memcmp(a.tiltAngle, b.tiltAngle, sizeof(float) * a.starCount) == 0 &&
            std::memcmp(a.colors, b.colors, sizeof(float) * 3 * a.starCount) == 0 &&
            std::memcmp(a.mags, b.mags, sizeof(float) * a.starCount) == 0 &&
            std::memcmp(a.kinds, b.kinds, sizeof(uint8_t) * a.starCount) == 0 &&
            std::memcmp(&a.params, &b.params, sizeof(GalaxylibGalaxyParams)) == 0;
        const bool differentSeedDiffers =
            c.starCount != a.starCount ||
            std::memcmp(a.orbitA, c.orbitA, sizeof(float) * a.starCount) != 0;

        if (!sameSeedIdentical) {
            std::fprintf(stderr, "FAIL: determinism: same inputs gave different bytes\n");
            ok = false;
        }
        if (!differentSeedDiffers) {
            std::fprintf(stderr, "FAIL: determinism: different seed gave identical orbitA\n");
            ok = false;
        }
    }

    if (ok) {
        pass("determinism");
    } else {
        ++g_failures;
    }

    galaxylib_free_cloud(&a);
    galaxylib_free_cloud(&b);
    galaxylib_free_cloud(&c);
}

// ---------------------------------------------------------------------------
// 2. Every type generates: STAR count == requested, no HAZE, sane params.
// ---------------------------------------------------------------------------

void checkGeneratesForAllTypes() {
    static const char* kTypeNames[GALAXYLIB_TYPE_COUNT] = {
        "spiral", "barred", "elliptical", "irregular", "starburst", "dust-belt-lenticular",
    };
    bool ok = true;
    for (uint32_t type = 0; type < GALAXYLIB_TYPE_COUNT; ++type) {
        GalaxylibCloud cloud{};
        const uint32_t requested = 500 + type * 137;
        const int rc = galaxylib_generate_galaxy(999ULL + type, type, requested, nullptr, nullptr, &cloud);
        if (rc != 0) {
            std::fprintf(stderr, "FAIL: generates -- type %u (%s): rc=%d (want 0)\n", type, kTypeNames[type],
                         rc);
            ok = false;
            galaxylib_free_cloud(&cloud);
            continue;
        }
        const KindCounts counts = countKinds(cloud);
        if (counts.star != requested) {
            std::fprintf(stderr, "FAIL: generates -- type %u (%s): STAR-kind count=%u (want %u)\n", type,
                         kTypeNames[type], counts.star, requested);
            ok = false;
        }
        if (counts.haze != 0) {
            std::fprintf(stderr, "FAIL: generates -- type %u (%s): HAZE-kind count=%u (want 0)\n", type,
                         kTypeNames[type], counts.haze);
            ok = false;
        }

        const GalaxylibGalaxyParams& p = cloud.params;
        if (!(p.radCoreN > 0.0f && p.radCoreN < 1.0f)) {
            std::fprintf(stderr, "FAIL: generates -- type %u (%s): radCoreN=%f not in (0,1)\n", type,
                         kTypeNames[type], static_cast<double>(p.radCoreN));
            ok = false;
        }
        if (!(p.exInner > 0.0f && p.exInner <= 2.0f) || !(p.exOuter > 0.0f && p.exOuter <= 2.0f)) {
            std::fprintf(stderr, "FAIL: generates -- type %u (%s): exInner=%f exOuter=%f not in (0,2]\n", type,
                         kTypeNames[type], static_cast<double>(p.exInner), static_cast<double>(p.exOuter));
            ok = false;
        }
        if (!(p.h2SizeMax > 0.0f) || !(p.h2Threshold > 0.0f)) {
            std::fprintf(stderr, "FAIL: generates -- type %u (%s): h2SizeMax=%f h2Threshold=%f not positive\n",
                         type, kTypeNames[type], static_cast<double>(p.h2SizeMax),
                         static_cast<double>(p.h2Threshold));
            ok = false;
        }
        if (std::fabs(p.radFarFieldN - 2.0f) > 1e-4f) {
            std::fprintf(stderr, "FAIL: generates -- type %u (%s): radFarFieldN=%f (want 2.0)\n", type,
                         kTypeNames[type], static_cast<double>(p.radFarFieldN));
            ok = false;
        }
        const bool wantBar = (type == GALAXYLIB_TYPE_BARRED);
        if (wantBar != (p.barRadiusN > 0.0f)) {
            std::fprintf(stderr, "FAIL: generates -- type %u (%s): barRadiusN=%f (want %s)\n", type,
                         kTypeNames[type], static_cast<double>(p.barRadiusN), wantBar ? ">0" : "==0");
            ok = false;
        }

        galaxylib_free_cloud(&cloud);
    }
    if (ok) {
        pass("all types generate");
    } else {
        ++g_failures;
    }
}

// ---------------------------------------------------------------------------
// 3. Validation: bad inputs fail nonzero, zero *out, and never call progress.
// ---------------------------------------------------------------------------

struct ProgressCounter {
    int calls = 0;
};

void countingProgress(const GalaxylibCloud*, float, void* user) {
    static_cast<ProgressCounter*>(user)->calls++;
}

void checkValidation() {
    bool ok = true;

    auto expectFail = [&](const char* label, uint64_t seed, uint32_t type, uint32_t starCount) {
        GalaxylibCloud cloud{};
        cloud.orbitA = reinterpret_cast<float*>(0x1);  // poison -- proves zeroing actually happens
        cloud.orbitB = reinterpret_cast<float*>(0x1);
        cloud.theta0 = reinterpret_cast<float*>(0x1);
        cloud.velTheta = reinterpret_cast<float*>(0x1);
        cloud.tiltAngle = reinterpret_cast<float*>(0x1);
        cloud.colors = reinterpret_cast<float*>(0x1);
        cloud.mags = reinterpret_cast<float*>(0x1);
        cloud.kinds = reinterpret_cast<uint8_t*>(0x1);
        cloud.starCount = 777;
        cloud.params.radCoreN = 0.5f;
        ProgressCounter counter;
        const int rc = galaxylib_generate_galaxy(seed, type, starCount, countingProgress, &counter, &cloud);
        if (rc == 0) {
            std::fprintf(stderr, "FAIL: validation -- %s: expected nonzero rc, got 0\n", label);
            ok = false;
            galaxylib_free_cloud(&cloud);
            return;
        }
        if (cloud.orbitA != nullptr || cloud.orbitB != nullptr || cloud.theta0 != nullptr ||
            cloud.velTheta != nullptr || cloud.tiltAngle != nullptr || cloud.colors != nullptr ||
            cloud.mags != nullptr || cloud.kinds != nullptr || cloud.starCount != 0) {
            std::fprintf(stderr, "FAIL: validation -- %s: *out not zeroed on failure\n", label);
            ok = false;
        }
        GalaxylibGalaxyParams zeroParams{};
        if (std::memcmp(&cloud.params, &zeroParams, sizeof(zeroParams)) != 0) {
            std::fprintf(stderr, "FAIL: validation -- %s: *out.params not zeroed on failure\n", label);
            ok = false;
        }
        if (counter.calls != 0) {
            std::fprintf(stderr, "FAIL: validation -- %s: progress called %d time(s) on a failing call\n",
                         label, counter.calls);
            ok = false;
        }
    };

    expectFail("type == GALAXYLIB_TYPE_COUNT", 1ULL, GALAXYLIB_TYPE_COUNT, 100);
    expectFail("starCount == 0", 1ULL, 0, 0);
    expectFail("starCount == 1000001", 1ULL, 0, 1000001);

    {
        const int rc = galaxylib_generate_galaxy(1ULL, 0, 100, nullptr, nullptr, nullptr);
        if (rc == 0) {
            std::fprintf(stderr, "FAIL: validation -- out == NULL: expected nonzero rc, got 0\n");
            ok = false;
        }
    }

    if (ok) {
        pass("input validation");
    } else {
        ++g_failures;
    }
}

// ---------------------------------------------------------------------------
// 4. Progress: fractions strictly increase in (0,1), snapshots are prefixes
//    of the final result, and NULL progress gives the same output.
// ---------------------------------------------------------------------------

struct ProgressCapture {
    std::vector<float> fractions;
    std::vector<std::vector<float>> orbitAPrefixes;
    std::vector<uint32_t> prefixCounts;
};

void capturingProgress(const GalaxylibCloud* snapshot, float fraction, void* user) {
    auto* capture = static_cast<ProgressCapture*>(user);
    capture->fractions.push_back(fraction);
    capture->prefixCounts.push_back(snapshot->starCount);
    capture->orbitAPrefixes.emplace_back(snapshot->orbitA,
                                         snapshot->orbitA + static_cast<std::size_t>(snapshot->starCount));
}

void checkProgress() {
    constexpr uint64_t seed = 777ULL;
    constexpr uint32_t type = 0;
    constexpr uint32_t starCount = 5000;

    ProgressCapture capture;
    GalaxylibCloud withProgress{};
    const int rc1 =
        galaxylib_generate_galaxy(seed, type, starCount, capturingProgress, &capture, &withProgress);

    GalaxylibCloud withoutProgress{};
    const int rc2 = galaxylib_generate_galaxy(seed, type, starCount, nullptr, nullptr, &withoutProgress);

    if (rc1 != 0 || rc2 != 0) {
        std::fprintf(stderr, "FAIL: progress: rc=%d/%d\n", rc1, rc2);
        ++g_failures;
        galaxylib_free_cloud(&withProgress);
        galaxylib_free_cloud(&withoutProgress);
        return;
    }

    bool ok = true;

    if (capture.fractions.empty()) {
        std::fprintf(stderr, "FAIL: progress: no callbacks for %u stars\n", starCount);
        ok = false;
    }

    float lastFraction = 0.0f;
    for (std::size_t i = 0; i < capture.fractions.size(); ++i) {
        const float f = capture.fractions[i];
        if (!(f > 0.0f) || !(f < 1.0f)) {
            std::fprintf(stderr, "FAIL: progress -- fraction[%zu]=%f not strictly in (0,1)\n", i,
                         static_cast<double>(f));
            ok = false;
        }
        if (i > 0 && !(f > lastFraction)) {
            std::fprintf(stderr, "FAIL: progress: fraction[%zu]=%f did not increase from %f\n", i,
                         static_cast<double>(f), static_cast<double>(lastFraction));
            ok = false;
        }
        lastFraction = f;
    }

    // Each snapshot must byte-match the same prefix of the no-progress run.
    for (std::size_t i = 0; i < capture.prefixCounts.size(); ++i) {
        const uint32_t count = capture.prefixCounts[i];
        if (count == 0 || count >= withoutProgress.starCount) {
            std::fprintf(stderr, "FAIL: progress -- snapshot starCount=%u out of (0,%u)\n", count,
                         withoutProgress.starCount);
            ok = false;
            continue;
        }
        if (std::memcmp(capture.orbitAPrefixes[i].data(), withoutProgress.orbitA, sizeof(float) * count) != 0) {
            std::fprintf(stderr, "FAIL: progress: snapshot %zu (count=%u) is not a prefix\n",
                         i, count);
            ok = false;
        }
    }

    if (withProgress.starCount != withoutProgress.starCount ||
        std::memcmp(withProgress.orbitA, withoutProgress.orbitA, sizeof(float) * withProgress.starCount) !=
            0 ||
        std::memcmp(withProgress.orbitB, withoutProgress.orbitB, sizeof(float) * withProgress.starCount) !=
            0 ||
        std::memcmp(withProgress.theta0, withoutProgress.theta0, sizeof(float) * withProgress.starCount) !=
            0 ||
        std::memcmp(withProgress.velTheta, withoutProgress.velTheta, sizeof(float) * withProgress.starCount) !=
            0 ||
        std::memcmp(withProgress.tiltAngle, withoutProgress.tiltAngle,
                     sizeof(float) * withProgress.starCount) != 0 ||
        std::memcmp(withProgress.colors, withoutProgress.colors, sizeof(float) * 3 * withProgress.starCount) !=
            0 ||
        std::memcmp(withProgress.mags, withoutProgress.mags, sizeof(float) * withProgress.starCount) != 0 ||
        std::memcmp(withProgress.kinds, withoutProgress.kinds, sizeof(uint8_t) * withProgress.starCount) != 0) {
        std::fprintf(stderr, "FAIL: progress: NULL callback changed the result\n");
        ok = false;
    }

    if (ok) {
        pass("progress callbacks");
    } else {
        ++g_failures;
    }

    galaxylib_free_cloud(&withProgress);
    galaxylib_free_cloud(&withoutProgress);
}

// ---------------------------------------------------------------------------
// 5. `kinds` is non-decreasing, and each H2 point matches its H2_CORE twin by index.
// ---------------------------------------------------------------------------

void checkKindContiguityAndH2Pairing() {
    GalaxylibCloud cloud{};
    const int rc = galaxylib_generate_galaxy(42ULL, 1, 4000, nullptr, nullptr, &cloud);
    bool ok = (rc == 0);
    if (!ok) {
        std::fprintf(stderr, "FAIL: kind-contiguity -- setup call returned rc=%d\n", rc);
    } else {
        for (uint32_t i = 1; i < cloud.starCount; ++i) {
            if (cloud.kinds[i] < cloud.kinds[i - 1]) {
                std::fprintf(stderr, "FAIL: kind-contiguity -- kinds[%u]=%u < kinds[%u]=%u\n", i,
                             cloud.kinds[i], i - 1, cloud.kinds[i - 1]);
                ok = false;
                break;
            }
        }

        uint32_t h2Start = cloud.starCount, h2CoreStart = cloud.starCount, h2CoreEnd = cloud.starCount;
        for (uint32_t i = 0; i < cloud.starCount; ++i) {
            if (cloud.kinds[i] == GALAXYLIB_KIND_H2 && h2Start == cloud.starCount) {
                h2Start = i;
            }
            if (cloud.kinds[i] == GALAXYLIB_KIND_H2_CORE) {
                if (h2CoreStart == cloud.starCount) {
                    h2CoreStart = i;
                }
                h2CoreEnd = i + 1;
            }
        }
        const uint32_t h2End = h2CoreStart;  // H2 block ends where H2_CORE begins
        const uint32_t h2Count = (h2Start < h2End) ? (h2End - h2Start) : 0;
        const uint32_t h2CoreCount = (h2CoreStart < h2CoreEnd) ? (h2CoreEnd - h2CoreStart) : 0;
        if (h2Count != h2CoreCount) {
            std::fprintf(stderr, "FAIL: kind-contiguity -- H2 block size=%u != H2_CORE block size=%u\n",
                         h2Count, h2CoreCount);
            ok = false;
        } else {
            for (uint32_t k = 0; k < h2Count; ++k) {
                const uint32_t iHalo = h2Start + k;
                const uint32_t iCore = h2CoreStart + k;
                if (cloud.orbitA[iHalo] != cloud.orbitA[iCore] || cloud.orbitB[iHalo] != cloud.orbitB[iCore] ||
                    cloud.theta0[iHalo] != cloud.theta0[iCore] ||
                    cloud.velTheta[iHalo] != cloud.velTheta[iCore] ||
                    cloud.tiltAngle[iHalo] != cloud.tiltAngle[iCore]) {
                    std::fprintf(stderr, "FAIL: kind-contiguity: H2 %u and H2_CORE %u differ\n", iHalo, iCore);
                    ok = false;
                    break;
                }
            }
        }
    }
    if (ok) {
        pass("kind contiguity");
    } else {
        ++g_failures;
    }
    galaxylib_free_cloud(&cloud);
}

// ---------------------------------------------------------------------------
// 6. Exact per-kind counts; FILAMENT is seed-dependent and only bounded.
// ---------------------------------------------------------------------------

void checkExactRatiosForType(uint32_t type, uint32_t starCount, uint64_t seed) {
    GalaxylibCloud cloud{};
    const int rc = galaxylib_generate_galaxy(seed, type, starCount, nullptr, nullptr, &cloud);
    if (rc != 0) {
        std::fprintf(stderr, "FAIL: exact-ratios -- type %u: setup call returned rc=%d\n", type, rc);
        ++g_failures;
        return;
    }

    const ExpectedCounts want = expectedCounts(type, starCount);
    const KindCounts got = countKinds(cloud);
    const uint32_t filamentMax = want.filamentSeeds * 99u;
    const uint32_t sum = got.haze + got.star + got.dust + got.filament + got.h2 + got.h2Core;

    bool ok = true;
    if (got.star != want.star) {
        std::fprintf(stderr, "FAIL: exact-ratios -- type %u: star=%u (want %u)\n", type, got.star, want.star);
        ok = false;
    }
    if (got.dust != want.dust) {
        std::fprintf(stderr, "FAIL: exact-ratios -- type %u: dust=%u (want %u)\n", type, got.dust, want.dust);
        ok = false;
    }
    if (got.h2 != want.h2) {
        std::fprintf(stderr, "FAIL: exact-ratios -- type %u: h2=%u (want %u)\n", type, got.h2, want.h2);
        ok = false;
    }
    if (got.h2Core != want.h2) {
        std::fprintf(stderr, "FAIL: exact-ratios -- type %u: h2Core=%u, want %u\n", type, got.h2Core,
                     want.h2);
        ok = false;
    }
    if (got.filament > filamentMax) {
        std::fprintf(stderr, "FAIL: exact-ratios -- type %u: filament=%u, want <= %u (%u seeds)\n",
                     type, got.filament, filamentMax, want.filamentSeeds);
        ok = false;
    }
    if (sum != cloud.starCount) {
        std::fprintf(stderr, "FAIL: exact-ratios -- type %u: kind sum=%u, starCount=%u\n",
                     type, sum, cloud.starCount);
        ok = false;
    }

    if (ok) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "kind counts type=%u starCount=%u", type, starCount);
        pass(msg);
    } else {
        ++g_failures;
    }
    galaxylib_free_cloud(&cloud);
}

void checkExactRatios() {
    checkExactRatiosForType(GALAXYLIB_TYPE_SPIRAL, 3000, 2024ULL);
    checkExactRatiosForType(GALAXYLIB_TYPE_BARRED, 3700, 2025ULL);
    checkExactRatiosForType(GALAXYLIB_TYPE_ELLIPTICAL, 3000, 2026ULL);
    checkExactRatiosForType(GALAXYLIB_TYPE_IRREGULAR, 3000, 2027ULL);
    checkExactRatiosForType(GALAXYLIB_TYPE_STARBURST, 4000, 2028ULL);
    checkExactRatiosForType(GALAXYLIB_TYPE_DUST_BELT_LENTICULAR, 4000, 2029ULL);
}

// ---------------------------------------------------------------------------
// 7. Every type: deterministic, kind-contiguous, sane per-kind counts.
// ---------------------------------------------------------------------------

void checkAllTypesDeterministicAndContiguous() {
    bool ok = true;
    for (uint32_t type = 0; type < GALAXYLIB_TYPE_COUNT; ++type) {
        constexpr uint32_t starCount = 4000;
        const uint64_t seed = 9000ULL + type;

        GalaxylibCloud a{};
        GalaxylibCloud b{};
        const int rcA = galaxylib_generate_galaxy(seed, type, starCount, nullptr, nullptr, &a);
        const int rcB = galaxylib_generate_galaxy(seed, type, starCount, nullptr, nullptr, &b);
        if (rcA != 0 || rcB != 0) {
            std::fprintf(stderr, "FAIL: all-types -- type %u: rc=%d/%d (want 0/0)\n", type, rcA, rcB);
            ok = false;
            galaxylib_free_cloud(&a);
            galaxylib_free_cloud(&b);
            continue;
        }

        if (a.starCount != b.starCount || std::memcmp(a.orbitA, b.orbitA, sizeof(float) * a.starCount) != 0 ||
            std::memcmp(a.theta0, b.theta0, sizeof(float) * a.starCount) != 0) {
            std::fprintf(stderr, "FAIL: all-types -- type %u: two calls differ\n", type);
            ok = false;
        }

        for (uint32_t i = 1; i < a.starCount; ++i) {
            if (a.kinds[i] < a.kinds[i - 1]) {
                std::fprintf(stderr, "FAIL: all-types -- type %u: kinds not non-decreasing at %u\n", type, i);
                ok = false;
                break;
            }
        }

        const KindCounts counts = countKinds(a);
        if (counts.star != starCount) {
            std::fprintf(stderr, "FAIL: all-types -- type %u: star=%u (want %u)\n", type, counts.star,
                         starCount);
            ok = false;
        }
        if (type != GALAXYLIB_TYPE_ELLIPTICAL && counts.dust == 0) {
            std::fprintf(stderr, "FAIL: all-types -- type %u: dust=0 (want >0)\n", type);
            ok = false;
        }
        if (type == GALAXYLIB_TYPE_ELLIPTICAL && (counts.dust != 0 || counts.h2 != 0)) {
            std::fprintf(stderr, "FAIL: all-types -- elliptical: dust=%u h2=%u (want 0/0)\n", counts.dust,
                         counts.h2);
            ok = false;
        }
        if (counts.haze != 0 || counts.h2 != counts.h2Core) {
            std::fprintf(stderr, "FAIL: all-types -- type %u: haze=%u h2=%u h2Core=%u\n",
                         type, counts.haze, counts.h2, counts.h2Core);
            ok = false;
        }

        galaxylib_free_cloud(&a);
        galaxylib_free_cloud(&b);
    }
    if (ok) {
        pass("all-type determinism");
    } else {
        ++g_failures;
    }
}

// ---------------------------------------------------------------------------
// 8. Free zeroes the struct and is repeat- and NULL-safe.
// ---------------------------------------------------------------------------

void checkFree() {
    GalaxylibCloud cloud{};
    const int rc = galaxylib_generate_galaxy(1ULL, 0, 100, nullptr, nullptr, &cloud);
    if (rc != 0) {
        std::fprintf(stderr, "FAIL: free -- setup call returned rc=%d\n", rc);
        ++g_failures;
        return;
    }

    galaxylib_free_cloud(&cloud);
    GalaxylibGalaxyParams zeroParams{};
    const bool zeroed = cloud.orbitA == nullptr && cloud.orbitB == nullptr && cloud.theta0 == nullptr &&
                         cloud.velTheta == nullptr && cloud.tiltAngle == nullptr && cloud.colors == nullptr &&
                         cloud.mags == nullptr && cloud.kinds == nullptr && cloud.starCount == 0 &&
                         std::memcmp(&cloud.params, &zeroParams, sizeof(zeroParams)) == 0;
    if (!zeroed) {
        std::fprintf(stderr, "FAIL: free -- galaxylib_free_cloud did not zero the struct\n");
        ++g_failures;
        return;
    }

    galaxylib_free_cloud(&cloud);   // repeat free on an already-zeroed struct
    galaxylib_free_cloud(nullptr);  // NULL

    pass("free");
}

// ---------------------------------------------------------------------------
// 9. Every color channel in [0,1], every mag > 0.
// ---------------------------------------------------------------------------

void checkColorsAndMagsSanity() {
    GalaxylibCloud cloud{};
    const int rc = galaxylib_generate_galaxy(31337ULL, 1, 3000, nullptr, nullptr, &cloud);
    if (rc != 0) {
        std::fprintf(stderr, "FAIL: colors/mags -- setup call returned rc=%d\n", rc);
        ++g_failures;
        return;
    }

    bool ok = true;
    for (uint32_t i = 0; i < cloud.starCount; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float v = cloud.colors[i * 3 + c];
            if (!(v >= 0.0f) || !(v <= 1.0f)) {
                std::fprintf(stderr, "FAIL: colors/mags -- colors[%u][%d]=%f not in [0,1]\n", i, c,
                             static_cast<double>(v));
                ok = false;
            }
        }
        if (!(cloud.mags[i] > 0.0f)) {
            std::fprintf(stderr, "FAIL: colors/mags -- mags[%u]=%f not positive\n", i,
                         static_cast<double>(cloud.mags[i]));
            ok = false;
        }
    }

    if (ok) {
        pass("colors and mags");
    } else {
        ++g_failures;
    }
    galaxylib_free_cloud(&cloud);
}

// ---------------------------------------------------------------------------
// 10. Informational: 60k-star bake wall time per type (target ~1-2 s).
// ---------------------------------------------------------------------------

void checkTiming() {
    static const char* kTypeNames[GALAXYLIB_TYPE_COUNT] = {
        "spiral", "barred", "elliptical", "irregular", "starburst", "dust-belt-lenticular",
    };
    bool ok = true;
    for (uint32_t type = 0; type < GALAXYLIB_TYPE_COUNT; ++type) {
        GalaxylibCloud cloud{};
        const auto start = std::chrono::steady_clock::now();
        const int rc = galaxylib_generate_galaxy(2026ULL + type, type, 60000, nullptr, nullptr, &cloud);
        const auto end = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(end - start).count();

        if (rc != 0) {
            std::fprintf(stderr, "FAIL: timing -- type %u (%s) 60000-star bake returned rc=%d\n", type,
                         kTypeNames[type], rc);
            ok = false;
            continue;
        }
        std::printf("INFO: type %u (%s) 60000-star bake wall time = %.3f s (%u total points)\n", type,
                    kTypeNames[type], seconds, cloud.starCount);
        galaxylib_free_cloud(&cloud);
    }
    if (ok) {
        pass("timing");
    } else {
        ++g_failures;
    }
}

}  // namespace

int main() {
    checkDeterminism();
    checkGeneratesForAllTypes();
    checkValidation();
    checkProgress();
    checkKindContiguityAndH2Pairing();
    checkExactRatios();
    checkAllTypesDeterministicAndContiguous();
    checkFree();
    checkColorsAndMagsSanity();
    checkTiming();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All checks PASSED\n");
    return 0;
}
