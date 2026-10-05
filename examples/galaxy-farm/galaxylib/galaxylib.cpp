// Plain C++ behind the C ABI -- no Qt, ordo or GL.

// The generation model is a faithful port of beltoforion/Galaxy-Renderer's
// Galaxy::InitStarsAndDust and its excentricity / tilt / orbital-velocity /
// blackbody helpers, redistributed under its license:
//   Copyright 2026 Ingo Berg. All rights reserved.
//   https://github.com/beltoforion/Galaxy-Renderer -- BSD-2-Clause.

// Emits orbit data only; the renderer derives positions, sizes and alphas.

// Math runs in double at parsec scale; only orbitA/orbitB are normalized
// (divided by the galaxy radius) when written.
#include "galaxylib.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// ---------------------------------------------------------------------------
// splitmix64, the only randomness source. Each point draws from its own
// stream off the caller's seed, so bakes are deterministic and thread-safe.
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

uint64_t mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Fixed per-population stream tags; never reuse.
constexpr uint64_t kTagPresetPick = 0x50524553'4554ULL;  // "PRESET"
constexpr uint64_t kTagStar = 0x5354'4152ULL;
constexpr uint64_t kTagDust = 0x4455'5354ULL;
constexpr uint64_t kTagFilament = 0x46494C41ULL;
constexpr uint64_t kTagH2 = 0x00000048'3200ULL;

uint64_t streamSeed(uint64_t seed, uint64_t tag, uint32_t index) {
    uint64_t h = mix64(seed ^ (tag * 0x9E3779B97F4A7C15ULL));
    h = mix64(h + static_cast<uint64_t>(index) * 0xBF58476D1CE4E5B9ULL);
    return h;
}

// ---------------------------------------------------------------------------
// Blackbody temperature -> RGB, 200 entries over 1000..10000 K.
// ---------------------------------------------------------------------------

constexpr int kBlackbodyEntries = 200;
constexpr double kBlackbodyMinTemp = 1000.0;
constexpr double kBlackbodyMaxTemp = 10000.0;

// clang-format off
constexpr double kBlackbodyTable[kBlackbodyEntries][3] = {
    {1, -0.00987248, -0.0166818},
    {1, 0.000671682, -0.0173831},
    {1, 0.0113477, -0.0179839},
    {1, 0.0221357, -0.0184684},
    {1, 0.0330177, -0.0188214},
    {1, 0.0439771, -0.0190283},
    {1, 0.0549989, -0.0190754},
    {1, 0.0660696, -0.0189496},
    {1, 0.0771766, -0.0186391},
    {1, 0.0883086, -0.0181329},
    {1, 0.0994553, -0.017421},
    {1, 0.110607, -0.0164945},
    {1, 0.121756, -0.0153455},
    {1, 0.132894, -0.0139671},
    {1, 0.144013, -0.0123534},
    {1, 0.155107, -0.0104993},
    {1, 0.166171, -0.0084008},
    {1, 0.177198, -0.00605465},
    {1, 0.188184, -0.00345843},
    {1, 0.199125, -0.000610485},
    {1, 0.210015, 0.00249014},
    {1, 0.220853, 0.00584373},
    {1, 0.231633, 0.00944995},
    {1, 0.242353, 0.0133079},
    {1, 0.25301, 0.0174162},
    {1, 0.263601, 0.021773},
    {1, 0.274125, 0.0263759},
    {1, 0.284579, 0.0312223},
    {1, 0.294962, 0.0363091},
    {1, 0.305271, 0.0416328},
    {1, 0.315505, 0.0471899},
    {1, 0.325662, 0.0529765},
    {1, 0.335742, 0.0589884},
    {1, 0.345744, 0.0652213},
    {1, 0.355666, 0.0716707},
    {1, 0.365508, 0.078332},
    {1, 0.375268, 0.0852003},
    {1, 0.384948, 0.0922709},
    {1, 0.394544, 0.0995389},
    {1, 0.404059, 0.106999},
    {1, 0.41349, 0.114646},
    {1, 0.422838, 0.122476},
    {1, 0.432103, 0.130482},
    {1, 0.441284, 0.138661},
    {1, 0.450381, 0.147005},
    {1, 0.459395, 0.155512},
    {1, 0.468325, 0.164175},
    {1, 0.477172, 0.172989},
    {1, 0.485935, 0.181949},
    {1, 0.494614, 0.19105},
    {1, 0.503211, 0.200288},
    {1, 0.511724, 0.209657},
    {1, 0.520155, 0.219152},
    {1, 0.528504, 0.228769},
    {1, 0.536771, 0.238502},
    {1, 0.544955, 0.248347},
    {1, 0.553059, 0.2583},
    {1, 0.561082, 0.268356},
    {1, 0.569024, 0.27851},
    {1, 0.576886, 0.288758},
    {1, 0.584668, 0.299095},
    {1, 0.592372, 0.309518},
    {1, 0.599996, 0.320022},
    {1, 0.607543, 0.330603},
    {1, 0.615012, 0.341257},
    {1, 0.622403, 0.35198},
    {1, 0.629719, 0.362768},
    {1, 0.636958, 0.373617},
    {1, 0.644122, 0.384524},
    {1, 0.65121, 0.395486},
    {1, 0.658225, 0.406497},
    {1, 0.665166, 0.417556},
    {1, 0.672034, 0.428659},
    {1, 0.678829, 0.439802},
    {1, 0.685552, 0.450982},
    {1, 0.692204, 0.462196},
    {1, 0.698786, 0.473441},
    {1, 0.705297, 0.484714},
    {1, 0.711739, 0.496013},
    {1, 0.718112, 0.507333},
    {1, 0.724417, 0.518673},
    {1, 0.730654, 0.53003},
    {1, 0.736825, 0.541402},
    {1, 0.742929, 0.552785},
    {1, 0.748968, 0.564177},
    {1, 0.754942, 0.575576},
    {1, 0.760851, 0.586979},
    {1, 0.766696, 0.598385},
    {1, 0.772479, 0.609791},
    {1, 0.778199, 0.621195},
    {1, 0.783858, 0.632595},
    {1, 0.789455, 0.643989},
    {1, 0.794991, 0.655375},
    {1, 0.800468, 0.666751},
    {1, 0.805886, 0.678116},
    {1, 0.811245, 0.689467},
    {1, 0.816546, 0.700803},
    {1, 0.82179, 0.712122},
    {1, 0.826976, 0.723423},
    {1, 0.832107, 0.734704},
    {1, 0.837183, 0.745964},
    {1, 0.842203, 0.757201},
    {1, 0.847169, 0.768414},
    {1, 0.852082, 0.779601},
    {1, 0.856941, 0.790762},
    {1, 0.861748, 0.801895},
    {1, 0.866503, 0.812999},
    {1, 0.871207, 0.824073},
    {1, 0.87586, 0.835115},
    {1, 0.880463, 0.846125},
    {1, 0.885017, 0.857102},
    {1, 0.889521, 0.868044},
    {1, 0.893977, 0.878951},
    {1, 0.898386, 0.889822},
    {1, 0.902747, 0.900657},
    {1, 0.907061, 0.911453},
    {1, 0.91133, 0.922211},
    {1, 0.915552, 0.932929},
    {1, 0.91973, 0.943608},
    {1, 0.923863, 0.954246},
    {1, 0.927952, 0.964842},
    {1, 0.931998, 0.975397},
    {1, 0.936001, 0.985909},
    {1, 0.939961, 0.996379},
    {0.993241, 0.9375, 1},
    {0.983104, 0.931743, 1},
    {0.973213, 0.926103, 1},
    {0.963562, 0.920576, 1},
    {0.954141, 0.915159, 1},
    {0.944943, 0.909849, 1},
    {0.935961, 0.904643, 1},
    {0.927189, 0.899538, 1},
    {0.918618, 0.894531, 1},
    {0.910244, 0.88962, 1},
    {0.902059, 0.884801, 1},
    {0.894058, 0.880074, 1},
    {0.886236, 0.875434, 1},
    {0.878586, 0.87088, 1},
    {0.871103, 0.86641, 1},
    {0.863783, 0.862021, 1},
    {0.856621, 0.857712, 1},
    {0.849611, 0.853479, 1},
    {0.84275, 0.849322, 1},
    {0.836033, 0.845239, 1},
    {0.829456, 0.841227, 1},
    {0.823014, 0.837285, 1},
    {0.816705, 0.83341, 1},
    {0.810524, 0.829602, 1},
    {0.804468, 0.825859, 1},
    {0.798532, 0.82218, 1},
    {0.792715, 0.818562, 1},
    {0.787012, 0.815004, 1},
    {0.781421, 0.811505, 1},
    {0.775939, 0.808063, 1},
    {0.770561, 0.804678, 1},
    {0.765287, 0.801348, 1},
    {0.760112, 0.798071, 1},
    {0.755035, 0.794846, 1},
    {0.750053, 0.791672, 1},
    {0.745164, 0.788549, 1},
    {0.740364, 0.785474, 1},
    {0.735652, 0.782448, 1},
    {0.731026, 0.779468, 1},
    {0.726482, 0.776534, 1},
    {0.722021, 0.773644, 1},
    {0.717638, 0.770798, 1},
    {0.713333, 0.767996, 1},
    {0.709103, 0.765235, 1},
    {0.704947, 0.762515, 1},
    {0.700862, 0.759835, 1},
    {0.696848, 0.757195, 1},
    {0.692902, 0.754593, 1},
    {0.689023, 0.752029, 1},
    {0.685208, 0.749502, 1},
    {0.681458, 0.747011, 1},
    {0.67777, 0.744555, 1},
    {0.674143, 0.742134, 1},
    {0.670574, 0.739747, 1},
    {0.667064, 0.737394, 1},
    {0.663611, 0.735073, 1},
    {0.660213, 0.732785, 1},
    {0.656869, 0.730528, 1},
    {0.653579, 0.728301, 1},
    {0.65034, 0.726105, 1},
    {0.647151, 0.723939, 1},
    {0.644013, 0.721801, 1},
    {0.640922, 0.719692, 1},
    {0.637879, 0.717611, 1},
    {0.634883, 0.715558, 1},
    {0.631932, 0.713531, 1},
    {0.629025, 0.711531, 1},
    {0.626162, 0.709557, 1},
    {0.623342, 0.707609, 1},
    {0.620563, 0.705685, 1},
    {0.617825, 0.703786, 1},
    {0.615127, 0.701911, 1},
    {0.612469, 0.70006, 1},
    {0.609848, 0.698231, 1},
    {0.607266, 0.696426, 1},
    {0.60472, 0.694643, 1},
};
// clang-format on

struct Rgb {
    double r = 0.0, g = 0.0, b = 0.0;
};

Rgb blackbodyColor(double tempK) {
    // Past 10000 K, blend toward the blue limit instead of clamping.
    if (tempK > kBlackbodyMaxTemp) {
        const double t = clamp01((tempK - kBlackbodyMaxTemp) / 10000.0);
        const auto& last = kBlackbodyTable[kBlackbodyEntries - 1];
        constexpr double kBlueLimit[3] = {0.45, 0.62, 1.0};
        return {clamp01(last[0] + (kBlueLimit[0] - last[0]) * t),
                clamp01(last[1] + (kBlueLimit[1] - last[1]) * t),
                clamp01(last[2] + (kBlueLimit[2] - last[2]) * t)};
    }
    int idx = static_cast<int>((tempK - kBlackbodyMinTemp) / (kBlackbodyMaxTemp - kBlackbodyMinTemp) *
                                kBlackbodyEntries);
    idx = std::max(0, std::min(kBlackbodyEntries - 1, idx));
    return {clamp01(kBlackbodyTable[idx][0]), clamp01(kBlackbodyTable[idx][1]), clamp01(kBlackbodyTable[idx][2])};
}

// ---------------------------------------------------------------------------
// Radius CDF of a bulge+disc intensity profile (Simpson rule), inverted so a
// uniform draw follows the profile. Parsec scale.
// ---------------------------------------------------------------------------

class RadiusCdf {
public:
    // i0 peak intensity, k bulge falloff, a disc scale length; `steps` intervals over [lo, hi).
    void setupRealistic(double i0, double k, double a, double rBulge, double lo, double hi, int steps) {
        i0_ = i0;
        k_ = k;
        a_ = a;
        rBulge_ = rBulge;
        lo_ = lo;
        hi_ = hi;
        steps_ = steps;
        build();
    }

    // Inverse CDF: a uniform p in [0,1) -> a radius sample.
    double valFromProb(double p) const {
        p = clamp01(p);
        const double h = 1.0 / static_cast<double>(y2_.size() - 1);
        int i = static_cast<int>(p / h);
        i = std::min(i, static_cast<int>(m2_.size()) - 1);
        const double remainder = p - static_cast<double>(i) * h;
        return y2_[static_cast<std::size_t>(i)] + m2_[static_cast<std::size_t>(i)] * remainder;
    }

private:
    double intensity(double x) const {
        const double bulge = i0_ * std::exp(-k_ * std::pow(x, 0.25));
        if (x < rBulge_) {
            return bulge;
        }
        const double bulgeAtEdge = i0_ * std::exp(-k_ * std::pow(rBulge_, 0.25));
        return bulgeAtEdge * std::exp(-(x - rBulge_) / a_);
    }

    void build() {
        const double h = (hi_ - lo_) / static_cast<double>(steps_);
        std::vector<double> x1{0.0}, y1{0.0}, m1;
        double y = 0.0;
        for (int i = 0; i < steps_; i += 2) {
            const double x = h * static_cast<double>(i + 2);
            y += h / 3.0 *
                 (intensity(lo_ + i * h) + 4.0 * intensity(lo_ + (i + 1) * h) + intensity(lo_ + (i + 2) * h));
            m1.push_back((y - y1.back()) / (2.0 * h));
            x1.push_back(x);
            y1.push_back(y);
        }
        m1.push_back(0.0);
        const double total = y1.back();
        for (std::size_t i = 0; i < y1.size(); ++i) {
            y1[i] /= total;
            m1[i] /= total;
        }

        y2_.assign(1, 0.0);
        const double hp = 1.0 / static_cast<double>(steps_);
        std::size_t k = 0;
        for (int i = 1; i < steps_; ++i) {
            const double p = static_cast<double>(i) * hp;
            while (y1[k + 1] <= p) {
                ++k;
            }
            const double radius = x1[k] + (p - y1[k]) / m1[k];
            m2_.push_back((radius - y2_.back()) / hp);
            y2_.push_back(radius);
        }
        m2_.push_back(0.0);
    }

    double i0_ = 1.0, k_ = 0.02, a_ = 1.0 / 3.0, rBulge_ = 0.3, lo_ = 0.0, hi_ = 2.0;
    int steps_ = 1000;
    std::vector<double> y2_;
    std::vector<double> m2_;
};

// ---------------------------------------------------------------------------
// Density-wave geometry: eccentricity and tilt by radius (parsecs).
// ---------------------------------------------------------------------------

struct GalaxyGeometryPc {
    double radCore = 0.0;
    double radGalaxy = 0.0;
    double radFarField = 0.0;
    double ex1 = 0.0;  // eccentricity at the core edge
    double ex2 = 0.0;  // eccentricity at the disc edge
    double angleOffset = 0.0;  // radians per parsec
    bool hasBar = false;
    double barRadius = 0.0;  // parsecs
    double barEx = 0.0;      // axis ratio of the bar orbits
};

double excentricity(const GalaxyGeometryPc& g, double r) {
    if (r < g.radCore) {
        if (g.hasBar && g.barRadius > 0.0) {
            if (r < g.barRadius) {
                return 1.0 + (r / g.barRadius) * (g.barEx - 1.0);
            }
            return g.barEx + (r - g.barRadius) / (g.radCore - g.barRadius) * (g.ex1 - g.barEx);
        }
        return 1.0 + (r / g.radCore) * (g.ex1 - 1.0);
    }
    if (r <= g.radGalaxy) {
        return g.ex1 + (r - g.radCore) / (g.radGalaxy - g.radCore) * (g.ex2 - g.ex1);
    }
    if (r < g.radFarField) {
        return g.ex2 + (r - g.radGalaxy) / (g.radFarField - g.radGalaxy) * (1.0 - g.ex2);
    }
    return 1.0;
}

double tiltAt(const GalaxyGeometryPc& g, double r) {
    if (g.hasBar && r < g.barRadius) {
        return g.barRadius * g.angleOffset;  // bar body: every orbit shares one orientation
    }
    return r * g.angleOffset;
}

// ---------------------------------------------------------------------------
// Orbital velocity with the dark-matter mass model. Radii must be in
// parsecs; the constants are calibrated at that scale.
// ---------------------------------------------------------------------------

constexpr double kPcToKm = 3.08567758129e13;
constexpr double kSecPerYear = 365.25 * 86400.0;
constexpr double kConstantOfGravity = 6.672e-11;

double massDisc(double r) {
    constexpr double d = 2000.0, rho0 = 1.0, rH = 2000.0;
    return rho0 * std::exp(-r / rH) * (r * r) * kPi * d;
}

double massHalo(double r) {
    constexpr double rhoH0 = 0.15, rC = 2500.0;
    return rhoH0 * (1.0 / (1.0 + (r / rC) * (r / rC))) * (4.0 * kPi * (r * r * r) / 3.0);
}

double velocityWithDarkMatterKms(double r) {
    constexpr double kMz = 100.0;
    return 20000.0 * std::sqrt(kConstantOfGravity * (massHalo(r) + massDisc(r) + kMz) / r);
}

double orbitalVelocityDegPerYear(double radiusPc) {
    // Guard r == 0, which would put a NaN in the output.
    const double r = std::max(radiusPc, 1e-6);
    const double velKms = velocityWithDarkMatterKms(r);
    const double circumferenceKm = 2.0 * kPi * r * kPcToKm;
    const double periodYears = circumferenceKm / (velKms * kSecPerYear);
    return 360.0 / periodYears;
}

// ---------------------------------------------------------------------------
// CPU-load knob: deterministic per-point busy work, folded into a negligible
// theta0 nudge so it can't be optimized out. Tune only kBusyIterations.
// ---------------------------------------------------------------------------

constexpr int kBusyIterations = 26;
constexpr double kBusyEpsilonDeg = 1e-6;

double busyLoopEpsilon(SplitMix64& rng) {
    const double phase0 = rng.range(0.0, 2.0 * kPi);
    const double phase1 = rng.range(0.0, 2.0 * kPi);
    const double phase2 = rng.range(0.0, 2.0 * kPi);
    auto axisNoise = [](double phase) {
        double amp = 1.0, freq = 1.0, n = 0.0;
        for (int oct = 0; oct < 3; ++oct) {
            n += amp * std::sin(freq * phase);
            amp *= 0.5;
            freq *= 2.13;
        }
        return n;
    };
    double sum = 0.0;
    for (int iter = 0; iter < kBusyIterations; ++iter) {
        const double t = static_cast<double>(iter) * 1.7;
        sum += axisNoise(phase0 + t) + axisNoise(phase1 + t) + axisNoise(phase2 + t);
    }
    return sum * (kBusyEpsilonDeg / static_cast<double>(kBusyIterations));
}

// ---------------------------------------------------------------------------
// Galaxy-Renderer's nine presets. All have numDust == numStars and
// numH2/numStars == 1/100.
// ---------------------------------------------------------------------------

struct GalaxyPreset {
    double radius;          // parsecs
    double coreRadius;      // parsecs
    double angularOffset;   // radians per parsec
    double exInner, exOuter;
    int numStars;
    int numDust;   // -1 => numStars, per Galaxy::Reset
    int numH2;
    int pertN;
    double pertAmp;
    double dustRenderSize;
    double baseTemp;   // Kelvin
    double h2SizeMax;
    double h2Threshold;
};

// clang-format off
constexpr GalaxyPreset kPresets[9] = {
    /* Galaxy 1.txt */ {13000, 4000, 0.0004,  0.85, 0.95, 40000, -1, 400, 2, 40, 90,  3600, 100, 1.2},
    /* Galaxy 2.txt */ {16000, 4000, 0.0003,  0.8,  0.85, 40000, -1, 400, 0, 40, 100, 4500, 100, 1.2},
    /* Galaxy 3.txt */ {13000, 4000, 0.00064, 0.9,  0.9,  40000, -1, 400, 0, 0,  85,  4100, 100, 1.2},
    /* Galaxy 4.txt */ {13000, 4000, 0.0004,  1.35, 1.05, 40000, -1, 400, 0, 0,  70,  4500, 100, 1.2},
    /* Galaxy 5.txt */ {13000, 4500, 0.0002,  0.65, 0.95, 40000, -1, 400, 3, 72, 90,  4000, 100, 1.2},
    /* Galaxy 6.txt */ {15000, 4000, 0.0003,  1.45, 1.0,  40000, -1, 400, 0, 0,  100, 4500, 100, 1.2},
    /* Galaxy 7.txt */ {14000, 12500,0.0002,  0.65, 0.95, 40000, -1, 400, 3, 72, 85,  2200, 100, 1.2},
    /* Galaxy 8.txt */ {13000, 1500, 0.0004,  1.1,  1.0,  40000, -1, 400, 1, 20, 80,  2800, 100, 1.2},
    /* Galaxy 9.txt */ {13000, 4000, 0.0004,  0.85, 0.95, 40000, -1, 400, 1, 20, 80,  4500, 100, 1.2},
};
// clang-format on

constexpr int kPresetCount = 9;

uint32_t pickPresetIndex(uint64_t seed) { return static_cast<uint32_t>(mix64(seed ^ kTagPresetPick) % kPresetCount); }

// Bar for GALAXYLIB_TYPE_BARRED (presets carry none).
constexpr double kBarRadiusFrac = 0.4;
constexpr double kBarEx = 0.5;

// ---------------------------------------------------------------------------
// Type -> seed-picked preset plus per-type overrides.
// ---------------------------------------------------------------------------

struct GalaxyCounts {
    uint32_t starCount = 0;
    uint32_t dustCount = 0;
    uint32_t h2Count = 0;
    uint32_t filamentSeedCount = 0;
};

struct ResolvedGalaxy {
    GalaxyGeometryPc geo;
    GalaxyCounts counts;
    double baseTemp = 0.0;  // Kelvin, post type-override
    GalaxylibGalaxyParams params{};
};

ResolvedGalaxy resolveGalaxy(uint64_t seed, uint32_t type, uint32_t starCount) {
    const GalaxyPreset& preset = kPresets[pickPresetIndex(seed)];

    ResolvedGalaxy rg;
    rg.geo.radGalaxy = preset.radius;
    rg.geo.radCore = preset.coreRadius;
    rg.geo.radFarField = preset.radius * 2.0;  // Galaxy::Reset: _radFarField = _radGalaxy * 2
    rg.geo.angleOffset = preset.angularOffset;
    rg.geo.ex1 = preset.exInner;
    rg.geo.ex2 = preset.exOuter;

    double baseTemp = preset.baseTemp;
    int pertN = preset.pertN;
    double pertAmp = preset.pertAmp;
    double dustMultiplier = 1.0;
    double h2Multiplier = 1.0;

    switch (type) {
        case GALAXYLIB_TYPE_BARRED:
            rg.geo.hasBar = true;
            rg.geo.barRadius = kBarRadiusFrac * rg.geo.radCore;
            rg.geo.barEx = kBarEx;
            break;
        case GALAXYLIB_TYPE_ELLIPTICAL:
            // Circular orbits everywhere: no arms.
            rg.geo.ex1 = 1.0;
            rg.geo.ex2 = 1.0;
            dustMultiplier = 0.0;
            h2Multiplier = 0.0;
            baseTemp += 800.0;
            break;
        case GALAXYLIB_TYPE_IRREGULAR:
            // pertAmp is a divisor: smaller is stronger (presets use 20-72).
            pertN = 3;
            pertAmp = 15.0;
            break;
        case GALAXYLIB_TYPE_STARBURST:
            baseTemp += 1500.0;
            h2Multiplier = 3.0;
            break;
        case GALAXYLIB_TYPE_DUST_BELT_LENTICULAR:
            dustMultiplier = 0.3;
            rg.geo.ex1 = (rg.geo.ex1 + 0.95) * 0.5;
            rg.geo.ex2 = (rg.geo.ex2 + 0.95) * 0.5;
            break;
        case GALAXYLIB_TYPE_SPIRAL:
        default:
            break;
    }

    rg.counts.starCount = starCount;
    rg.counts.dustCount = static_cast<uint32_t>(std::llround(static_cast<double>(starCount) * dustMultiplier));
    const auto h2Base = static_cast<uint32_t>(
        std::llround(static_cast<double>(starCount) * static_cast<double>(preset.numH2) /
                     static_cast<double>(preset.numStars)));
    rg.counts.h2Count = static_cast<uint32_t>(std::llround(static_cast<double>(h2Base) * h2Multiplier));
    rg.counts.filamentSeedCount = rg.counts.dustCount / 100u;

    rg.baseTemp = baseTemp;

    rg.params.radCoreN = static_cast<float>(rg.geo.radCore / rg.geo.radGalaxy);
    rg.params.radFarFieldN = static_cast<float>(rg.geo.radFarField / rg.geo.radGalaxy);
    rg.params.exInner = static_cast<float>(rg.geo.ex1);
    rg.params.exOuter = static_cast<float>(rg.geo.ex2);
    rg.params.angleOffsetN = static_cast<float>(rg.geo.angleOffset * rg.geo.radGalaxy);
    rg.params.barRadiusN = rg.geo.hasBar ? static_cast<float>(rg.geo.barRadius / rg.geo.radGalaxy) : 0.0f;
    rg.params.barEx = static_cast<float>(rg.geo.barEx);
    rg.params.pertN = pertN;
    rg.params.pertAmp = static_cast<float>(pertAmp);
    rg.params.dustRenderSize = static_cast<float>(preset.dustRenderSize);
    rg.params.h2SizeMax = static_cast<float>(preset.h2SizeMax);
    rg.params.h2Threshold = static_cast<float>(preset.h2Threshold);
    rg.params.baseTemp = static_cast<float>(baseTemp);

    return rg;
}

// ---------------------------------------------------------------------------
// Output buffer writer. Points must be emitted in kind order.
// ---------------------------------------------------------------------------

struct CloudWriter {
    float* orbitA = nullptr;
    float* orbitB = nullptr;
    float* theta0 = nullptr;
    float* velTheta = nullptr;
    float* tiltAngle = nullptr;
    float* colors = nullptr;
    float* mags = nullptr;
    uint8_t* kinds = nullptr;
    uint32_t idx = 0;
    uint32_t total = 0;
    uint32_t checkpointStride = 1;
    // Parsecs -> galaxy-local units (radius 1), applied to orbitA/orbitB only.
    double invRadGalaxyPc = 1.0;
    GalaxylibProgressFn progress = nullptr;
    void* user = nullptr;
    GalaxylibCloud view{};  // reused for every checkpoint snapshot

    void emit(double a, double b, double theta0Deg, double velThetaDegPerYear, double tiltAngleRad,
              const Rgb& color, double mag, GalaxylibKind kind) {
        orbitA[idx] = static_cast<float>(a * invRadGalaxyPc);
        orbitB[idx] = static_cast<float>(b * invRadGalaxyPc);
        theta0[idx] = static_cast<float>(theta0Deg);
        velTheta[idx] = static_cast<float>(velThetaDegPerYear);
        tiltAngle[idx] = static_cast<float>(tiltAngleRad);
        colors[idx * 3 + 0] = static_cast<float>(color.r);
        colors[idx * 3 + 1] = static_cast<float>(color.g);
        colors[idx * 3 + 2] = static_cast<float>(color.b);
        mags[idx] = static_cast<float>(mag);
        kinds[idx] = static_cast<uint8_t>(kind);
        ++idx;

        // No checkpoint at idx == total: the finished cloud is the return value.
        if (progress != nullptr && idx % checkpointStride == 0 && idx != total) {
            view.starCount = idx;
            progress(&view, static_cast<float>(idx) / static_cast<float>(total), user);
        }
    }
};

// ---------------------------------------------------------------------------
// Stars. Velocity uses the radius directly, not (a+b)/2 like the others (as in Galaxy-Renderer).
// ---------------------------------------------------------------------------

void generateStars(CloudWriter& writer, uint64_t seed, const GalaxyGeometryPc& geo, const RadiusCdf& cdf,
                    uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        SplitMix64 rng(streamSeed(seed, kTagStar, i));
        const double radPc = cdf.valFromProb(rng.nextUnit());
        const double a = radPc;
        const double b = radPc * excentricity(geo, radPc);
        const double tilt = tiltAt(geo, radPc);
        double theta0 = rng.range(0.0, 360.0);
        const double velTheta = orbitalVelocityDegPerYear(a);
        const double temp = 6000.0 + rng.range(-2000.0, 2000.0);
        double mag = rng.range(0.1, 0.5);
        if (i < count / 60u) {
            mag = std::min(mag + rng.range(0.1, 0.5), 1.0);
        }
        theta0 += busyLoopEpsilon(rng);  // CPU-load knob

        writer.emit(a, b, theta0, velTheta, tilt, blackbodyColor(temp), mag, GALAXYLIB_KIND_STAR);
    }
}

// ---------------------------------------------------------------------------
// Dust. Odd indices draw uniformly to fill the inter-arm field.
// ---------------------------------------------------------------------------

void generateDust(CloudWriter& writer, uint64_t seed, const GalaxyGeometryPc& geo, const RadiusCdf& cdf,
                   double baseTemp, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        SplitMix64 rng(streamSeed(seed, kTagDust, i));
        double radPc;
        if ((i & 1u) == 0u) {
            radPc = cdf.valFromProb(rng.nextUnit());
        } else {
            const double x = rng.range(-geo.radGalaxy, geo.radGalaxy);
            const double y = rng.range(-geo.radGalaxy, geo.radGalaxy);
            radPc = std::sqrt(x * x + y * y);
        }
        const double a = radPc;
        const double b = radPc * excentricity(geo, radPc);
        const double tilt = tiltAt(geo, radPc);
        double theta0 = rng.range(0.0, 360.0);
        const double velTheta = orbitalVelocityDegPerYear((a + b) * 0.5);
        // Artistic ramp: blue outside, yellow inside.
        const double temp = baseTemp + radPc / 4.5;
        const double mag = rng.range(0.02, 0.17);
        theta0 += busyLoopEpsilon(rng);

        writer.emit(a, b, theta0, velTheta, tilt, blackbodyColor(temp), mag, GALAXYLIB_KIND_DUST);
    }
}

// ---------------------------------------------------------------------------
// Filaments: each seed walks a short random chain of links.
// countFilamentPoints and generateFilaments must draw identically via initFilamentSeed.
// ---------------------------------------------------------------------------

struct FilamentSeed {
    double radPc = 0.0;
    double thetaBaseDeg = 0.0;
    double magBase = 0.0;
    uint32_t numLinks = 0;
};

FilamentSeed initFilamentSeed(SplitMix64& rng, double radGalaxyPc) {
    FilamentSeed s;
    const double x = rng.range(-radGalaxyPc, radGalaxyPc);
    const double y = rng.range(-radGalaxyPc, radGalaxyPc);
    s.radPc = std::sqrt(x * x + y * y);
    s.thetaBaseDeg = rng.range(0.0, 360.0);
    s.magBase = rng.range(0.1, 0.15);
    s.numLinks = static_cast<uint32_t>(rng.range(0.0, 100.0));  // Galaxy-Renderer: (int)(100 * rnum())
    return s;
}

uint32_t countFilamentPoints(uint64_t seed, const GalaxyGeometryPc& geo, uint32_t seedCount) {
    uint32_t total = 0;
    for (uint32_t s = 0; s < seedCount; ++s) {
        SplitMix64 rng(streamSeed(seed, kTagFilament, s));
        total += initFilamentSeed(rng, geo.radGalaxy).numLinks;
    }
    return total;
}

void generateFilaments(CloudWriter& writer, uint64_t seed, const GalaxyGeometryPc& geo, double baseTemp,
                        uint32_t seedCount) {
    for (uint32_t s = 0; s < seedCount; ++s) {
        SplitMix64 rng(streamSeed(seed, kTagFilament, s));
        const FilamentSeed fseed = initFilamentSeed(rng, geo.radGalaxy);
        double radPc = fseed.radPc;

        for (uint32_t link = 0; link < fseed.numLinks; ++link) {
            radPc = radPc + rng.range(-200.0, 200.0);  // Galaxy-Renderer: rad + 200 - 400*rnum()
            const double a = radPc;
            const double b = radPc * excentricity(geo, radPc);
            const double tilt = tiltAt(geo, radPc);
            double theta0 = fseed.thetaBaseDeg + rng.range(-10.0, 10.0);
            const double velTheta = orbitalVelocityDegPerYear((a + b) * 0.5);
            const double temp = baseTemp + radPc / 4.5 - 1000.0;
            const double mag = fseed.magBase + rng.range(0.0, 0.025);
            theta0 += busyLoopEpsilon(rng);

            writer.emit(a, b, theta0, velTheta, tilt, blackbodyColor(temp), mag, GALAXYLIB_KIND_FILAMENT);
        }
    }
}

// ---------------------------------------------------------------------------
// H2 regions: each particle is emitted twice (halo block, then core block),
// regenerated bit-for-bit from its per-index stream.
// ---------------------------------------------------------------------------

struct H2Point {
    double a = 0.0, b = 0.0, theta0 = 0.0, velTheta = 0.0, tilt = 0.0, temp = 0.0, mag = 0.0;
};

H2Point computeH2Point(SplitMix64& rng, const GalaxyGeometryPc& geo) {
    H2Point p;
    const double x = rng.range(-geo.radGalaxy, geo.radGalaxy);
    const double y = rng.range(-geo.radGalaxy, geo.radGalaxy);
    const double radPc = std::sqrt(x * x + y * y);
    p.a = radPc;
    p.b = radPc * excentricity(geo, radPc);
    p.tilt = tiltAt(geo, radPc);
    p.theta0 = rng.range(0.0, 360.0);
    p.velTheta = orbitalVelocityDegPerYear((p.a + p.b) * 0.5);
    p.temp = rng.range(3000.0, 9000.0);
    p.mag = rng.range(0.1, 0.15);
    p.theta0 += busyLoopEpsilon(rng);
    return p;
}

void generateH2(CloudWriter& writer, uint64_t seed, const GalaxyGeometryPc& geo, uint32_t count,
                GalaxylibKind kind) {
    for (uint32_t i = 0; i < count; ++i) {
        SplitMix64 rng(streamSeed(seed, kTagH2, i));
        const H2Point p = computeH2Point(rng, geo);
        writer.emit(p.a, p.b, p.theta0, p.velTheta, p.tilt, blackbodyColor(p.temp), p.mag, kind);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// C ABI
// ---------------------------------------------------------------------------

extern "C" {

int galaxylib_generate_galaxy(uint64_t seed, uint32_t type, uint32_t starCount, GalaxylibProgressFn progress,
                               void* user, GalaxylibCloud* out) {
    if (out == nullptr) {
        return 1;
    }
    if (type >= GALAXYLIB_TYPE_COUNT) {
        std::memset(out, 0, sizeof(*out));
        return 2;
    }
    if (starCount < 1 || starCount > 1000000) {
        std::memset(out, 0, sizeof(*out));
        return 3;
    }

    const ResolvedGalaxy rg = resolveGalaxy(seed, type, starCount);

    RadiusCdf cdf;
    cdf.setupRealistic(1.0, 0.02, rg.geo.radGalaxy / 3.0, rg.geo.radCore, 0.0, rg.geo.radFarField, 1000);

    // Filament total is seed-dependent; counted up front to size the buffers.
    const uint32_t filamentTotal = countFilamentPoints(seed, rg.geo, rg.counts.filamentSeedCount);
    const uint32_t totalCount =
        rg.counts.starCount + rg.counts.dustCount + filamentTotal + 2u * rg.counts.h2Count;

    auto* orbitA = static_cast<float*>(std::malloc(sizeof(float) * totalCount));
    auto* orbitB = static_cast<float*>(std::malloc(sizeof(float) * totalCount));
    auto* theta0 = static_cast<float*>(std::malloc(sizeof(float) * totalCount));
    auto* velTheta = static_cast<float*>(std::malloc(sizeof(float) * totalCount));
    auto* tiltAngle = static_cast<float*>(std::malloc(sizeof(float) * totalCount));
    auto* colors = static_cast<float*>(std::malloc(sizeof(float) * 3 * totalCount));
    auto* mags = static_cast<float*>(std::malloc(sizeof(float) * totalCount));
    auto* kinds = static_cast<uint8_t*>(std::malloc(sizeof(uint8_t) * totalCount));
    if (!orbitA || !orbitB || !theta0 || !velTheta || !tiltAngle || !colors || !mags || !kinds) {
        std::free(orbitA);
        std::free(orbitB);
        std::free(theta0);
        std::free(velTheta);
        std::free(tiltAngle);
        std::free(colors);
        std::free(mags);
        std::free(kinds);
        std::memset(out, 0, sizeof(*out));
        return 4;
    }

    CloudWriter writer;
    writer.orbitA = orbitA;
    writer.orbitB = orbitB;
    writer.theta0 = theta0;
    writer.velTheta = velTheta;
    writer.tiltAngle = tiltAngle;
    writer.colors = colors;
    writer.mags = mags;
    writer.kinds = kinds;
    writer.invRadGalaxyPc = 1.0 / rg.geo.radGalaxy;
    writer.total = totalCount;
    writer.checkpointStride = std::max<uint32_t>(1, totalCount / 10);
    writer.progress = progress;
    writer.user = user;
    writer.view.orbitA = orbitA;
    writer.view.orbitB = orbitB;
    writer.view.theta0 = theta0;
    writer.view.velTheta = velTheta;
    writer.view.tiltAngle = tiltAngle;
    writer.view.colors = colors;
    writer.view.mags = mags;
    writer.view.kinds = kinds;
    writer.view.params = rg.params;

    // Kind order is an ABI guarantee: star, dust, filament, H2 halo, H2 core.
    generateStars(writer, seed, rg.geo, cdf, rg.counts.starCount);
    generateDust(writer, seed, rg.geo, cdf, rg.baseTemp, rg.counts.dustCount);
    generateFilaments(writer, seed, rg.geo, rg.baseTemp, rg.counts.filamentSeedCount);
    generateH2(writer, seed, rg.geo, rg.counts.h2Count, GALAXYLIB_KIND_H2);
    generateH2(writer, seed, rg.geo, rg.counts.h2Count, GALAXYLIB_KIND_H2_CORE);

    out->orbitA = orbitA;
    out->orbitB = orbitB;
    out->theta0 = theta0;
    out->velTheta = velTheta;
    out->tiltAngle = tiltAngle;
    out->colors = colors;
    out->mags = mags;
    out->kinds = kinds;
    out->starCount = totalCount;
    out->params = rg.params;
    return 0;
}

void galaxylib_free_cloud(GalaxylibCloud* cloud) {
    if (cloud == nullptr) {
        return;
    }
    std::free(cloud->orbitA);
    std::free(cloud->orbitB);
    std::free(cloud->theta0);
    std::free(cloud->velTheta);
    std::free(cloud->tiltAngle);
    std::free(cloud->colors);
    std::free(cloud->mags);
    std::free(cloud->kinds);
    std::memset(cloud, 0, sizeof(*cloud));  // zeroed struct tolerates a repeat free
}

}  // extern "C"
