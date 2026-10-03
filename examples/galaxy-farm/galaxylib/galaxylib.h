// C ABI for a deterministic, pure-CPU galaxy generator, built as a plain
// shared library with no Qt/ordo/OpenGL dependency. The generation model is
// a faithful port of beltoforion's Galaxy-Renderer (BSD-2-Clause, Copyright
// 2026 Ingo Berg, https://github.com/beltoforion/Galaxy-Renderer -- full
// notice in galaxylib.cpp): points are emitted as per-point ORBIT data plus
// color/mag; final positions, sizes, and alphas are the renderer's job.
#pragma once

#include <stdint.h>

#if defined(_WIN32)
    #if defined(GALAXYLIB_BUILD)
        #define GALAXYLIB_API __declspec(dllexport)
    #else
        #define GALAXYLIB_API __declspec(dllimport)
    #endif
#else
    #define GALAXYLIB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Per-galaxy constants the renderer needs alongside the orbit data.
// Fields ending in `N` are normalized (galaxy radius == 1.0).
typedef struct GalaxylibGalaxyParams {
    float radCoreN;        /* core radius, normalized (radGalaxy = 1) */
    float radFarFieldN;    /* far-field radius, normalized; always 2.0 */
    float exInner, exOuter; /* excentricity at the core edge / disc edge */
    float angleOffsetN;    /* ellipse tilt per normalized radius unit, radians */
    float barRadiusN;      /* 0 = no bar */
    float barEx;           /* axis ratio of the bar orbits */
    int   pertN;           /* density-wave arm perturbation count */
    float pertAmp;         /* perturbation amplitude divisor: smaller = stronger */
    float dustRenderSize;  /* px size class for the dust pass */
    float h2SizeMax;       /* px size of a fully-ignited H2 region */
    float h2Threshold;     /* arm-crest density enhancement needed to ignite */
    float baseTemp;        /* Kelvin, feeds the dust/filament temperature ramp */
} GalaxylibGalaxyParams;

// One baked galaxy in normalized units (galaxy radius == 1.0).
// Buffers are owned by galaxylib; free only via galaxylib_free_cloud.
// `kinds` is non-decreasing (one contiguous range per kind).
// H2 point i and H2_CORE point i are the same particle.
typedef struct GalaxylibCloud {
    float*   orbitA;     /* semi-major axis, normalized units */
    float*   orbitB;     /* semi-minor axis */
    float*   theta0;     /* initial parametric angle, DEGREES */
    float*   velTheta;   /* angular velocity, DEGREES per time unit */
    float*   tiltAngle;  /* ellipse tilt, RADIANS */
    float*   colors;     /* rgb per point, 0..1, blackbody from temperature;
                             mag and per-kind tint are left to the renderer */
    float*   mags;       /* brightness/size magnitude */
    uint8_t* kinds;      /* GalaxylibKind, non-decreasing */
    uint32_t starCount;  /* TOTAL point count across all kinds */
    GalaxylibGalaxyParams params;
} GalaxylibCloud;

// A point's population. HAZE is never emitted; kept for ABI stability.
typedef enum GalaxylibKind {
    GALAXYLIB_KIND_HAZE = 0,
    GALAXYLIB_KIND_STAR = 1,
    GALAXYLIB_KIND_DUST = 2,
    GALAXYLIB_KIND_FILAMENT = 3,  /* dust arranged into short chains */
    GALAXYLIB_KIND_H2 = 4,        /* arm-crest star-forming region, halo */
    GALAXYLIB_KIND_H2_CORE = 5,   /* same region's bright pinpoint core */
    GALAXYLIB_KIND_COUNT = 6,
} GalaxylibKind;

// Values for `type`: a seed-picked preset plus the noted override.
typedef enum GalaxylibGalaxyType {
    GALAXYLIB_TYPE_SPIRAL = 0,                  /* preset as shipped */
    GALAXYLIB_TYPE_BARRED = 1,                  /* preset + a central bar */
    GALAXYLIB_TYPE_ELLIPTICAL = 2,              /* preset + circular orbits, no dust/H2 */
    GALAXYLIB_TYPE_IRREGULAR = 3,               /* preset + strong 3-arm perturbation */
    GALAXYLIB_TYPE_STARBURST = 4,               /* preset + hot baseTemp, 3x H2 */
    GALAXYLIB_TYPE_DUST_BELT_LENTICULAR = 5,    /* preset + less dust, near-circular orbits */
    GALAXYLIB_TYPE_COUNT = 6,
} GalaxylibGalaxyType;

/* Called on the calling thread; `snapshot` is valid only during the call.
   Entries [0, snapshot->starCount) are already final and in `out`'s
   kind order. 0 < fraction < 1: there is no 0.0 or 1.0 call. */
typedef void (*GalaxylibProgressFn)(const GalaxylibCloud* snapshot,
                                    float fraction, void* user);

// Generates one galaxy's point cloud. Deterministic and thread-safe.
// `progress` is nullable and fires about every tenth, 0 < fraction < 1.
// `starCount` sizes the star population; out->starCount is the total.
// Returns 0 on success; nonzero with *out zeroed for NULL out, bad type, or
// starCount outside [1, 1000000]. `progress` is never called on failure.
GALAXYLIB_API int galaxylib_generate_galaxy(uint64_t seed, uint32_t type,
                                            uint32_t starCount,
                                            GalaxylibProgressFn progress,
                                            void* user, GalaxylibCloud* out);

// Frees and zeroes the cloud. Safe on a zeroed cloud and on NULL.
GALAXYLIB_API void galaxylib_free_cloud(GalaxylibCloud* cloud);

#ifdef __cplusplus
}
#endif
