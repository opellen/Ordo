#include "view/galaxy_gl_widget.h"

#include <algorithm>
#include <cmath>
#include <random>

#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QVector2D>
#include <QWheelEvent>

namespace {

// ---- Sector layout ---------------------------------------------------------
// Slots lie on a Fermat (golden-angle) spiral: radius ~ sqrt(slotIndex).
constexpr float kGoldenAngle = 2.399963229728653f;  // radians, ~137.5 degrees
constexpr float kRadiusStep = 5.0f;                 // world units between spiral rings
constexpr float kHeightAmplitude = 3.0f;
constexpr float kGalaxyWorldRadius = 1.5f;  // each galaxy's local radius-<=1 cloud scales to this

// Small enough to dive inside a galaxy; the near plane scales down to match.
constexpr float kMinDistance = 0.02f;
// The far plane scales with the dolly, so the sector never clips away.
constexpr float kMaxDistance = 50000.0f;
constexpr float kElevationLimit = 85.0f;
constexpr float kOrbitSensitivity = 0.4f;      // degrees per pixel of drag
constexpr double kZoomFactorPerNotch = 0.9994;  // exponential zoom base (~7% per notch)

// ---- Jump journey -----------------------------------------------------------
// The camera never moves; the sectors travel along the jump axis. The
// destination starts kJumpDistance ahead and lands exactly at the origin at
// transit01 = 1.
constexpr float kJumpDistance = 500.0f;
// Peak sideways bow of the journey, world units (bump 256 t^4 (1-t)^4 peaks at 1).
constexpr float kJumpCurve = 80.0f;

// ---- Jump streak field ------------------------------------------------------
// World-space local stars around the camera, stretched along the jump axis
// with journey speed.
constexpr int kStreakLaneCount = 1600;
// Cross-section half-extent of the field, world units.
constexpr float kStreakFieldRadius = 14.0f;

// Field lengths scrolled per full journey.
constexpr float kStreakSweeps = 12.5f;
constexpr float kStreakDriftPerSec = 0.02f;  // field lengths per second

// ---- Sky --------------------------------------------------------------------
// Per-sector skybox, deterministic from the sector coordinate.
const char* kSkyVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec2 inPos;

out vec2 vNdc;

void main() {
    vNdc = inPos;
    gl_Position = vec4(inPos, 0.0, 1.0);
}
)";

const char* kSkyFragmentShaderSource = R"(#version 330 core
in vec2 vNdc;
out vec4 fragColor;

uniform vec3 camRight;
uniform vec3 camUp;
uniform vec3 camForward;
uniform float tanHalfFov;
uniform float aspect;

// Pre-baked sky cubemaps.
uniform samplerCube skyCurrent;
uniform samplerCube skyDest;
uniform float jumpMix;   // journey SPEED (0->1->0 bell), drives the distortion
uniform float skyBlend;  // journey TRAVEL fraction, cross-fades sky A -> B

void main() {
    vec3 dir = normalize(camForward + vNdc.x * tanHalfFov * aspect * camRight +
                         vNdc.y * tanHalfFov * camUp);
    vec3 dirDistorted = dir;
    if (jumpMix > 0.001) {
        // Swirl rays around the jump axis (Rodrigues) and push them outward,
        // scaled by speed: a twisted tunnel at peak.
        vec3 axis = camForward;
        float ang = acos(clamp(dot(dir, axis), -1.0, 1.0));
        float swirl = jumpMix * 1.1 * smoothstep(0.15, 1.3, ang);
        float c = cos(swirl);
        float s = sin(swirl);
        dirDistorted = normalize(dir * c + cross(axis, dir) * s + axis * dot(axis, dir) * (1.0 - c));
        dirDistorted = normalize(dirDistorted - axis * (0.30 * jumpMix * smoothstep(0.2, 1.2, ang)));
    }
    // Both skies share one distortion; the destination fades in with travel.
    vec3 sky = mix(texture(skyCurrent, dirDistorted).rgb, texture(skyDest, dirDistorted).rgb,
                   clamp(skyBlend, 0.0, 1.0));
    fragColor = vec4(sky * (1.0 - 0.18 * jumpMix), 1.0);
}
)";

// Galaxy point shader -- a faithful port of beltoforion/Galaxy-Renderer's
// VertexBufferStars.hpp vertex/fragment pair (BSD-2-Clause, Copyright 2026
// Ingo Berg), plus a full mvp, a per-galaxy sizeFactor and depth-cue
// uniforms. kindMode selects the population per contiguous draw range.
const char* kStarVertexShaderSource = R"(#version 330 core
#define DEG_TO_RAD 0.01745329251
layout(location = 0) in float inA;
layout(location = 1) in float inB;
layout(location = 2) in float inTheta0;    // degrees
layout(location = 3) in float inVelTheta;  // degrees per time unit
layout(location = 4) in float inTilt;      // radians
layout(location = 5) in vec3 inColor;
layout(location = 6) in float inMag;

uniform mat4 mvp;
uniform float time;
// Scales fixed pixel sizes to this galaxy's on-screen size.
uniform float sizeFactor;
// 1 star, 2 dust, 3 filament, 4 H2 halo, 5 H2 core (GalaxylibKind values).
uniform int kindMode;
uniform float defocus;

// GalaxylibGalaxyParams, normalized units (galaxy radius = 1).
uniform float radCoreN;
uniform float radFarFieldN;
uniform float exInner;
uniform float exOuter;
uniform float angleOffsetN;
uniform float barRadiusN;
uniform float barEx;
uniform int pertN;
uniform float pertAmp;
uniform float dustSize;    // params.dustRenderSize
uniform float h2SizeMax;
uniform float h2Threshold;

out vec3 vColor;
out float vEnergy;

vec2 calcPos(float a, float b, float theta, float velTheta, float tiltAngle) {
    float thetaActual = theta + velTheta * time;
    float beta = -tiltAngle;
    float alpha = thetaActual * DEG_TO_RAD;
    float cosalpha = cos(alpha);
    float sinalpha = sin(alpha);
    float cosbeta = cos(beta);
    float sinbeta = sin(beta);
    vec2 ps = vec2(a * cosalpha * cosbeta - b * sinalpha * sinbeta,
                   a * cosalpha * sinbeta + b * sinalpha * cosbeta);
    if (pertAmp > 0.0 && pertN > 0) {
        ps.x += (a / pertAmp) * sin(alpha * 2.0 * pertN);
        ps.y += (a / pertAmp) * cos(alpha * 2.0 * pertN);
    }
    return ps;
}

// Density-wave excentricity at radius r; must match galaxylib's profile.
float excentricity(float r) {
    if (r < radCoreN) {
        if (barRadiusN > 0.0) {
            if (r < barRadiusN)
                return 1.0 + (r / barRadiusN) * (barEx - 1.0);
            return barEx + (r - barRadiusN) / (radCoreN - barRadiusN) * (exInner - barEx);
        }
        return 1.0 + (r / radCoreN) * (exInner - 1.0);
    }
    else if (r <= 1.0)
        return exInner + (r - radCoreN) / (1.0 - radCoreN) * (exOuter - exInner);
    else if (r < radFarFieldN)
        return exOuter + (r - 1.0) / (radFarFieldN - 1.0) * (1.0 - exOuter);
    else
        return 1.0;
}

float tiltAt(float r) {
    return ((barRadiusN > 0.0) ? max(r, barRadiusN) : r) * angleOffsetN;
}

// Star formation is suppressed inside the bar body (old, gas-poor).
float barFactor(float r) {
    return (barRadiusN > 0.0) ? smoothstep(0.6 * barRadiusN, 1.05 * barRadiusN, r) : 1.0;
}

void main() {
    vec2 ps = calcPos(inA, inB, inTheta0, inVelTheta, inTilt);
    float sizePx = 0.0;
    vec3 col = inColor * inMag;

    if (kindMode == 1) {
        sizePx = inMag * 4.0;
    } else if (kindMode == 2) {
        sizePx = inMag * 5.0 * dustSize;
    } else if (kindMode == 3) {
        sizePx = inMag * 2.0 * dustSize;
    } else {
        // H2 ignites where neighbouring density waves crowd together (arm
        // crest). delta = +-1000 pc of a 13000 pc disc, normalized.
        float delta = 0.077;
        float aI = max(inA - delta, 0.0);
        float dI = inA - aI;
        float aO = inA + delta;
        float tA = tiltAt(inA);
        float tI = tiltAt(aI);
        float tO = tiltAt(aO);
        vec2 psI = calcPos(aI, aI * excentricity(aI), inTheta0 - (tA - tI) / DEG_TO_RAD, inVelTheta, tI);
        vec2 psO = calcPos(aO, aO * excentricity(aO), inTheta0 + (tO - tA) / DEG_TO_RAD, inVelTheta, tO);
        float rho = 0.5 * (dI / max(distance(ps, psI), 1e-4) + delta / max(distance(ps, psO), 1e-4));
        float ignite = smoothstep(h2Threshold, 1.5 * h2Threshold, rho) * barFactor(inA);
        if (kindMode == 4) {
            sizePx = h2SizeMax * ignite;
            col = inColor * inMag * vec3(2.0, 0.5, 0.5) * ignite;
        } else {
            sizePx = h2SizeMax * ignite / 10.0;
            col = vec3(ignite);
        }
    }

    // The orbit plane is the galaxy's local XZ plane.
    gl_Position = mvp * vec4(ps.x, 0.0, ps.y, 1.0);
    // Sub-pixel points can vanish, so floor at 1 px and dim by
    // the area ratio to keep total light correct.
    float wanted = sizePx * sizeFactor;
    float px = clamp(wanted, 1.0, 512.0);
    gl_PointSize = px;
    vEnergy = min(1.0, (wanted * wanted) / (px * px));
    // Aerial perspective: distant galaxies desaturate toward gray.
    vColor = mix(col, vec3(dot(col, vec3(0.3333))), 0.7 * defocus);
}
)";

const char* kStarFragmentShaderSource = R"(#version 330 core
in vec3 vColor;
in float vEnergy;
out vec4 fragColor;

uniform int kindMode;
// Per-galaxy brightness. Blend is (SRC_ALPHA, ONE), so it scales added
// light linearly.
uniform float intensity;

void main() {
    vec2 circCoord = 2.0 * gl_PointCoord - 1.0;
    float d = length(circCoord);
    if (d > 1.0) discard;
    // Linear cone; dust and filaments are faint so their overlap forms the arm haze.
    float alpha = 1.0 - d;
    if (kindMode == 2) alpha *= 0.05;
    else if (kindMode == 3) alpha *= 0.07;
    fragColor = vec4(vColor * intensity * vEnergy, alpha);
}
)";

// Streak layer: one thin screen-space quad per lane around a world-space
// star. Accumulated travel drives axial position; speed gates only stretch.
const char* kStreakVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 inBase;   // cross-section u, v and axial phase a (0..1)
layout(location = 1) in float inT;     // 0 = trailing end, 1 = leading tip
layout(location = 2) in float inSide;  // -1 / +1 across the streak
layout(location = 3) in vec3 inColor;

uniform mat4 viewProj;   // the scene's projection * view -- FOV kick included
uniform vec3 camEye;
uniform vec3 axisFwd;    // frozen jump axis (unit) and its perpendicular basis
uniform vec3 axisRight;
uniform vec3 axisUp;
uniform vec3 lensDir;    // lens axis: camera -> destination sector center
uniform float lensTan;   // lens cone half-angle tangent, speed-gated
uniform float scroll;    // accumulated axial scroll in field lengths (travel + drift)
uniform float speed01;   // journey speed bell, 0 -> 1 -> 0
uniform float aspect;

out vec3 vColor;
out float vX;        // sprite-space coords, in core-half-width units
out float vY;
out float vSegHalf;  // half the stretched segment's length, same units
out float vFade;

// kFieldLength: axial repeat length (world units). kCameraAt: camera
// position within it. kStretch: beam length at full speed. kPad: quad
// margin in core-half-widths; the glow must fade out inside it.
const float kFieldLength = 40.0;
const float kCameraAt = 0.15;
const float kStretch = 5.0;
const float kPad = 2.5;

void main() {
    // a01: 0 just behind the camera, 1 at the far end; fract wraps stars
    // that fall behind back out ahead.
    float a01 = fract(inBase.z - scroll);
    float axial = (a01 - kCameraAt) * kFieldLength;

    // Zero length at rest: any floor reads as a pill up close.
    float len = kStretch * speed01 * (0.6 + 0.8 * fract(inBase.z * 31.7));
    float axHead = axial - 0.5 * len;
    float axTail = axial + 0.5 * len;

    // Lens: push each endpoint away from lensDir, r' = sqrt(r^2 + R^2) with
    // R growing with distance ahead (zero at rest).
    vec3 p0 = camEye + axisRight * inBase.x + axisUp * inBase.y;
    vec3 head = p0 + axisFwd * axHead;
    vec3 tail = p0 + axisFwd * axTail;
    for (int i = 0; i < 2; ++i) {
        vec3 pos = i == 0 ? head : tail;
        vec3 q = pos - camEye;
        float axL = dot(q, lensDir);
        vec3 rad = q - lensDir * axL;
        float r = length(rad);
        float R = max(axL, 0.0) * lensTan;
        float rp = sqrt(r * r + R * R);
        vec3 radDir = r > 1e-4 ? rad / r : axisRight;  // on-axis: any perpendicular works
        pos = camEye + lensDir * axL + radDir * rp;
        if (i == 0) { head = pos; } else { tail = pos; }
    }

    vec4 clipHead = viewProj * vec4(head, 1.0);
    vec4 clipTail = viewProj * vec4(tail, 1.0);
    vColor = inColor;
    // Cull segments at or behind the near plane; they would smear across the screen.
    if (min(clipHead.w, clipTail.w) < 0.1) {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
        vX = 0.0; vY = 0.0; vSegHalf = 0.0;
        vFade = 0.0;
        return;
    }

    // Aspect-corrected screen-space segment direction.
    vec2 ndcHead = clipHead.xy / clipHead.w;
    vec2 ndcTail = clipTail.xy / clipTail.w;
    vec2 d = (ndcHead - ndcTail) * vec2(aspect, 1.0);
    float dLen = length(d);
    vec2 dirS = dLen > 1e-5 ? d / dLen : vec2(1.0, 0.0);
    vec2 perp = vec2(-dirS.y, dirS.x);

    // Width scales with 1/w (near stars grow), one value for the whole quad.
    float wMid = 0.5 * (clipHead.w + clipTail.w);
    float coreHW = 0.0065 * (0.8 + 0.4 * speed01) * clamp(8.0 / wMid, 0.3, 4.0);
    float segHalf = 0.5 * dLen / coreHW;

    // Pad the quad by kPad on all sides so the glow ends inside it.
    vec4 clip = mix(clipTail, clipHead, inT);
    vec2 offs = (perp * inSide + dirS * (inT * 2.0 - 1.0)) * (coreHW * kPad);
    offs.x /= aspect;
    clip.xy += offs * clip.w;
    gl_Position = clip;

    vX = (inT * 2.0 - 1.0) * (segHalf + kPad);
    vY = inSide * kPad;
    vSegHalf = segHalf;

    // Brightness depends on depth only: zero at the far end, so the wrap
    // teleport is invisible.
    float depthFade = clamp((1.0 - a01) / 0.55, 0.0, 1.0);
    vFade = depthFade * mix(0.85, 1.0, speed01);
}
)";

const char* kStreakFragmentShaderSource = R"(#version 330 core
in vec3 vColor;
in float vX;
in float vY;
in float vSegHalf;
in float vFade;
out vec4 fragColor;

void main() {
    // Elliptical field: the axial semi-axis grows with the stretch (a round
    // dot at rest). e = 1 is the zero-intensity rim.
    const float kRadial = 2.4;
    float a = vSegHalf + kRadial;
    float e = sqrt((vX * vX) / (a * a) + (vY * vY) / (kRadial * kRadial));
    float dEq = e * kRadial;
    float core = exp(-dEq * dEq * 2.0);
    float halo = 0.30 * exp(-dEq * dEq * 0.55);
    float window = smoothstep(1.0, 0.85, e);
    // Brighter head, fading tail; gated by the stretch so a resting dot
    // stays symmetric.
    float headT = clamp(0.5 + 0.5 * vX / max(vSegHalf, 1e-3), 0.0, 1.0);
    float bias = mix(1.0, mix(0.6, 1.0, headT), clamp(vSegHalf * 0.7, 0.0, 1.0));
    float intensity = 1.25 * vFade * (core + halo) * window * bias;
    fragColor = vec4(vColor * intensity, intensity);
}
)";

// ---- Bloom post-process -----------------------------------------------------
// Scene into a float FBO, quarter-res bright extract, separable blur, composite.
constexpr float kBloomStrength = 0.9f;

const char* kFullscreenVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec2 inPos;

out vec2 vUv;

void main() {
    vUv = inPos * 0.5 + 0.5;
    gl_Position = vec4(inPos, 0.0, 1.0);
}
)";

// Port of space-3d's SKYBOX_NEBULA_VERT; v_position is the world-space
// sample direction (unit cube seen from its center at 90-degree FOV).
const char* kSkyNebulaBakeVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 inPos;

uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

out vec3 v_position;

void main() {
    v_position = inPos;
    gl_Position = projectionMatrix * viewMatrix * vec4(inPos, 1.0);
}
)";

// Nebula fragment shader from space-3d (wwwtyro; matusnovak's port).
// The following shader is based on space-3d shader by wwwtyro from https://github.com/wwwtyro/space-3d
const char* kSkyNebulaBakeFragmentShaderSource = R"(#version 330 core
// Source: https://github.com/wwwtyro/space-3d/blob/gh-pages/src/glsl/nebula.glsl
// created by: github.com/wwwtyro
// edited by: github.com/matusnovak

uniform vec4 uColor;
uniform vec3 uOffset;
uniform float uScale;
uniform float uIntensity;
uniform float uFalloff;

in vec3 v_position;

out vec4 fragmentColor;

//
// GLSL textureless classic 4D noise "cnoise",
// with an RSL-style periodic variant "pnoise".
// Author:  Stefan Gustavson (stefan.gustavson@liu.se)
// Version: 2011-08-22
//
// Many thanks to Ian McEwan of Ashima Arts for the
// ideas for permutation and gradient selection.
//
// Copyright (c) 2011 Stefan Gustavson. All rights reserved.
// Distributed under the MIT license. See LICENSE file.
// https://github.com/ashima/webgl-noise
//

vec4 mod289(vec4 x)
{
  return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec4 permute(vec4 x)
{
  return mod289(((x*34.0)+1.0)*x);
}

vec4 taylorInvSqrt(vec4 r)
{
  return 1.79284291400159 - 0.85373472095314 * r;
}

vec4 fade(vec4 t) {
  return t*t*t*(t*(t*6.0-15.0)+10.0);
}

// Classic Perlin noise
float cnoise(vec4 P)
{
  vec4 Pi0 = floor(P); // Integer part for indexing
  vec4 Pi1 = Pi0 + 1.0; // Integer part + 1
  Pi0 = mod289(Pi0);
  Pi1 = mod289(Pi1);
  vec4 Pf0 = fract(P); // Fractional part for interpolation
  vec4 Pf1 = Pf0 - 1.0; // Fractional part - 1.0
  vec4 ix = vec4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
  vec4 iy = vec4(Pi0.yy, Pi1.yy);
  vec4 iz0 = vec4(Pi0.zzzz);
  vec4 iz1 = vec4(Pi1.zzzz);
  vec4 iw0 = vec4(Pi0.wwww);
  vec4 iw1 = vec4(Pi1.wwww);

  vec4 ixy = permute(permute(ix) + iy);
  vec4 ixy0 = permute(ixy + iz0);
  vec4 ixy1 = permute(ixy + iz1);
  vec4 ixy00 = permute(ixy0 + iw0);
  vec4 ixy01 = permute(ixy0 + iw1);
  vec4 ixy10 = permute(ixy1 + iw0);
  vec4 ixy11 = permute(ixy1 + iw1);

  vec4 gx00 = ixy00 * (1.0 / 7.0);
  vec4 gy00 = floor(gx00) * (1.0 / 7.0);
  vec4 gz00 = floor(gy00) * (1.0 / 6.0);
  gx00 = fract(gx00) - 0.5;
  gy00 = fract(gy00) - 0.5;
  gz00 = fract(gz00) - 0.5;
  vec4 gw00 = vec4(0.75) - abs(gx00) - abs(gy00) - abs(gz00);
  vec4 sw00 = step(gw00, vec4(0.0));
  gx00 -= sw00 * (step(0.0, gx00) - 0.5);
  gy00 -= sw00 * (step(0.0, gy00) - 0.5);

  vec4 gx01 = ixy01 * (1.0 / 7.0);
  vec4 gy01 = floor(gx01) * (1.0 / 7.0);
  vec4 gz01 = floor(gy01) * (1.0 / 6.0);
  gx01 = fract(gx01) - 0.5;
  gy01 = fract(gy01) - 0.5;
  gz01 = fract(gz01) - 0.5;
  vec4 gw01 = vec4(0.75) - abs(gx01) - abs(gy01) - abs(gz01);
  vec4 sw01 = step(gw01, vec4(0.0));
  gx01 -= sw01 * (step(0.0, gx01) - 0.5);
  gy01 -= sw01 * (step(0.0, gy01) - 0.5);

  vec4 gx10 = ixy10 * (1.0 / 7.0);
  vec4 gy10 = floor(gx10) * (1.0 / 7.0);
  vec4 gz10 = floor(gy10) * (1.0 / 6.0);
  gx10 = fract(gx10) - 0.5;
  gy10 = fract(gy10) - 0.5;
  gz10 = fract(gz10) - 0.5;
  vec4 gw10 = vec4(0.75) - abs(gx10) - abs(gy10) - abs(gz10);
  vec4 sw10 = step(gw10, vec4(0.0));
  gx10 -= sw10 * (step(0.0, gx10) - 0.5);
  gy10 -= sw10 * (step(0.0, gy10) - 0.5);

  vec4 gx11 = ixy11 * (1.0 / 7.0);
  vec4 gy11 = floor(gx11) * (1.0 / 7.0);
  vec4 gz11 = floor(gy11) * (1.0 / 6.0);
  gx11 = fract(gx11) - 0.5;
  gy11 = fract(gy11) - 0.5;
  gz11 = fract(gz11) - 0.5;
  vec4 gw11 = vec4(0.75) - abs(gx11) - abs(gy11) - abs(gz11);
  vec4 sw11 = step(gw11, vec4(0.0));
  gx11 -= sw11 * (step(0.0, gx11) - 0.5);
  gy11 -= sw11 * (step(0.0, gy11) - 0.5);

  vec4 g0000 = vec4(gx00.x,gy00.x,gz00.x,gw00.x);
  vec4 g1000 = vec4(gx00.y,gy00.y,gz00.y,gw00.y);
  vec4 g0100 = vec4(gx00.z,gy00.z,gz00.z,gw00.z);
  vec4 g1100 = vec4(gx00.w,gy00.w,gz00.w,gw00.w);
  vec4 g0010 = vec4(gx10.x,gy10.x,gz10.x,gw10.x);
  vec4 g1010 = vec4(gx10.y,gy10.y,gz10.y,gw10.y);
  vec4 g0110 = vec4(gx10.z,gy10.z,gz10.z,gw10.z);
  vec4 g1110 = vec4(gx10.w,gy10.w,gz10.w,gw10.w);
  vec4 g0001 = vec4(gx01.x,gy01.x,gz01.x,gw01.x);
  vec4 g1001 = vec4(gx01.y,gy01.y,gz01.y,gw01.y);
  vec4 g0101 = vec4(gx01.z,gy01.z,gz01.z,gw01.z);
  vec4 g1101 = vec4(gx01.w,gy01.w,gz01.w,gw01.w);
  vec4 g0011 = vec4(gx11.x,gy11.x,gz11.x,gw11.x);
  vec4 g1011 = vec4(gx11.y,gy11.y,gz11.y,gw11.y);
  vec4 g0111 = vec4(gx11.z,gy11.z,gz11.z,gw11.z);
  vec4 g1111 = vec4(gx11.w,gy11.w,gz11.w,gw11.w);

  vec4 norm00 = taylorInvSqrt(vec4(dot(g0000, g0000), dot(g0100, g0100), dot(g1000, g1000), dot(g1100, g1100)));
  g0000 *= norm00.x;
  g0100 *= norm00.y;
  g1000 *= norm00.z;
  g1100 *= norm00.w;

  vec4 norm01 = taylorInvSqrt(vec4(dot(g0001, g0001), dot(g0101, g0101), dot(g1001, g1001), dot(g1101, g1101)));
  g0001 *= norm01.x;
  g0101 *= norm01.y;
  g1001 *= norm01.z;
  g1101 *= norm01.w;

  vec4 norm10 = taylorInvSqrt(vec4(dot(g0010, g0010), dot(g0110, g0110), dot(g1010, g1010), dot(g1110, g1110)));
  g0010 *= norm10.x;
  g0110 *= norm10.y;
  g1010 *= norm10.z;
  g1110 *= norm10.w;

  vec4 norm11 = taylorInvSqrt(vec4(dot(g0011, g0011), dot(g0111, g0111), dot(g1011, g1011), dot(g1111, g1111)));
  g0011 *= norm11.x;
  g0111 *= norm11.y;
  g1011 *= norm11.z;
  g1111 *= norm11.w;

  float n0000 = dot(g0000, Pf0);
  float n1000 = dot(g1000, vec4(Pf1.x, Pf0.yzw));
  float n0100 = dot(g0100, vec4(Pf0.x, Pf1.y, Pf0.zw));
  float n1100 = dot(g1100, vec4(Pf1.xy, Pf0.zw));
  float n0010 = dot(g0010, vec4(Pf0.xy, Pf1.z, Pf0.w));
  float n1010 = dot(g1010, vec4(Pf1.x, Pf0.y, Pf1.z, Pf0.w));
  float n0110 = dot(g0110, vec4(Pf0.x, Pf1.yz, Pf0.w));
  float n1110 = dot(g1110, vec4(Pf1.xyz, Pf0.w));
  float n0001 = dot(g0001, vec4(Pf0.xyz, Pf1.w));
  float n1001 = dot(g1001, vec4(Pf1.x, Pf0.yz, Pf1.w));
  float n0101 = dot(g0101, vec4(Pf0.x, Pf1.y, Pf0.z, Pf1.w));
  float n1101 = dot(g1101, vec4(Pf1.xy, Pf0.z, Pf1.w));
  float n0011 = dot(g0011, vec4(Pf0.xy, Pf1.zw));
  float n1011 = dot(g1011, vec4(Pf1.x, Pf0.y, Pf1.zw));
  float n0111 = dot(g0111, vec4(Pf0.x, Pf1.yzw));
  float n1111 = dot(g1111, Pf1);

  vec4 fade_xyzw = fade(Pf0);
  vec4 n_0w = mix(vec4(n0000, n1000, n0100, n1100), vec4(n0001, n1001, n0101, n1101), fade_xyzw.w);
  vec4 n_1w = mix(vec4(n0010, n1010, n0110, n1110), vec4(n0011, n1011, n0111, n1111), fade_xyzw.w);
  vec4 n_zw = mix(n_0w, n_1w, fade_xyzw.z);
  vec2 n_yzw = mix(n_zw.xy, n_zw.zw, fade_xyzw.y);
  float n_xyzw = mix(n_yzw.x, n_yzw.y, fade_xyzw.x);
  return 2.2 * n_xyzw;
}

// Classic Perlin noise, periodic version
float pnoise(vec4 P, vec4 rep)
{
  vec4 Pi0 = mod(floor(P), rep); // Integer part modulo rep
  vec4 Pi1 = mod(Pi0 + 1.0, rep); // Integer part + 1 mod rep
  Pi0 = mod289(Pi0);
  Pi1 = mod289(Pi1);
  vec4 Pf0 = fract(P); // Fractional part for interpolation
  vec4 Pf1 = Pf0 - 1.0; // Fractional part - 1.0
  vec4 ix = vec4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
  vec4 iy = vec4(Pi0.yy, Pi1.yy);
  vec4 iz0 = vec4(Pi0.zzzz);
  vec4 iz1 = vec4(Pi1.zzzz);
  vec4 iw0 = vec4(Pi0.wwww);
  vec4 iw1 = vec4(Pi1.wwww);

  vec4 ixy = permute(permute(ix) + iy);
  vec4 ixy0 = permute(ixy + iz0);
  vec4 ixy1 = permute(ixy + iz1);
  vec4 ixy00 = permute(ixy0 + iw0);
  vec4 ixy01 = permute(ixy0 + iw1);
  vec4 ixy10 = permute(ixy1 + iw0);
  vec4 ixy11 = permute(ixy1 + iw1);

  vec4 gx00 = ixy00 * (1.0 / 7.0);
  vec4 gy00 = floor(gx00) * (1.0 / 7.0);
  vec4 gz00 = floor(gy00) * (1.0 / 6.0);
  gx00 = fract(gx00) - 0.5;
  gy00 = fract(gy00) - 0.5;
  gz00 = fract(gz00) - 0.5;
  vec4 gw00 = vec4(0.75) - abs(gx00) - abs(gy00) - abs(gz00);
  vec4 sw00 = step(gw00, vec4(0.0));
  gx00 -= sw00 * (step(0.0, gx00) - 0.5);
  gy00 -= sw00 * (step(0.0, gy00) - 0.5);

  vec4 gx01 = ixy01 * (1.0 / 7.0);
  vec4 gy01 = floor(gx01) * (1.0 / 7.0);
  vec4 gz01 = floor(gy01) * (1.0 / 6.0);
  gx01 = fract(gx01) - 0.5;
  gy01 = fract(gy01) - 0.5;
  gz01 = fract(gz01) - 0.5;
  vec4 gw01 = vec4(0.75) - abs(gx01) - abs(gy01) - abs(gz01);
  vec4 sw01 = step(gw01, vec4(0.0));
  gx01 -= sw01 * (step(0.0, gx01) - 0.5);
  gy01 -= sw01 * (step(0.0, gy01) - 0.5);

  vec4 gx10 = ixy10 * (1.0 / 7.0);
  vec4 gy10 = floor(gx10) * (1.0 / 7.0);
  vec4 gz10 = floor(gy10) * (1.0 / 6.0);
  gx10 = fract(gx10) - 0.5;
  gy10 = fract(gy10) - 0.5;
  gz10 = fract(gz10) - 0.5;
  vec4 gw10 = vec4(0.75) - abs(gx10) - abs(gy10) - abs(gz10);
  vec4 sw10 = step(gw10, vec4(0.0));
  gx10 -= sw10 * (step(0.0, gx10) - 0.5);
  gy10 -= sw10 * (step(0.0, gy10) - 0.5);

  vec4 gx11 = ixy11 * (1.0 / 7.0);
  vec4 gy11 = floor(gx11) * (1.0 / 7.0);
  vec4 gz11 = floor(gy11) * (1.0 / 6.0);
  gx11 = fract(gx11) - 0.5;
  gy11 = fract(gy11) - 0.5;
  gz11 = fract(gz11) - 0.5;
  vec4 gw11 = vec4(0.75) - abs(gx11) - abs(gy11) - abs(gz11);
  vec4 sw11 = step(gw11, vec4(0.0));
  gx11 -= sw11 * (step(0.0, gx11) - 0.5);
  gy11 -= sw11 * (step(0.0, gy11) - 0.5);

  vec4 g0000 = vec4(gx00.x,gy00.x,gz00.x,gw00.x);
  vec4 g1000 = vec4(gx00.y,gy00.y,gz00.y,gw00.y);
  vec4 g0100 = vec4(gx00.z,gy00.z,gz00.z,gw00.z);
  vec4 g1100 = vec4(gx00.w,gy00.w,gz00.w,gw00.w);
  vec4 g0010 = vec4(gx10.x,gy10.x,gz10.x,gw10.x);
  vec4 g1010 = vec4(gx10.y,gy10.y,gz10.y,gw10.y);
  vec4 g0110 = vec4(gx10.z,gy10.z,gz10.z,gw10.z);
  vec4 g1110 = vec4(gx10.w,gy10.w,gz10.w,gw10.w);
  vec4 g0001 = vec4(gx01.x,gy01.x,gz01.x,gw01.x);
  vec4 g1001 = vec4(gx01.y,gy01.y,gz01.y,gw01.y);
  vec4 g0101 = vec4(gx01.z,gy01.z,gz01.z,gw01.z);
  vec4 g1101 = vec4(gx01.w,gy01.w,gz01.w,gw01.w);
  vec4 g0011 = vec4(gx11.x,gy11.x,gz11.x,gw11.x);
  vec4 g1011 = vec4(gx11.y,gy11.y,gz11.y,gw11.y);
  vec4 g0111 = vec4(gx11.z,gy11.z,gz11.z,gw11.z);
  vec4 g1111 = vec4(gx11.w,gy11.w,gz11.w,gw11.w);

  vec4 norm00 = taylorInvSqrt(vec4(dot(g0000, g0000), dot(g0100, g0100), dot(g1000, g1000), dot(g1100, g1100)));
  g0000 *= norm00.x;
  g0100 *= norm00.y;
  g1000 *= norm00.z;
  g1100 *= norm00.w;

  vec4 norm01 = taylorInvSqrt(vec4(dot(g0001, g0001), dot(g0101, g0101), dot(g1001, g1001), dot(g1101, g1101)));
  g0001 *= norm01.x;
  g0101 *= norm01.y;
  g1001 *= norm01.z;
  g1101 *= norm01.w;

  vec4 norm10 = taylorInvSqrt(vec4(dot(g0010, g0010), dot(g0110, g0110), dot(g1010, g1010), dot(g1110, g1110)));
  g0010 *= norm10.x;
  g0110 *= norm10.y;
  g1010 *= norm10.z;
  g1110 *= norm10.w;

  vec4 norm11 = taylorInvSqrt(vec4(dot(g0011, g0011), dot(g0111, g0111), dot(g1011, g1011), dot(g1111, g1111)));
  g0011 *= norm11.x;
  g0111 *= norm11.y;
  g1011 *= norm11.z;
  g1111 *= norm11.w;

  float n0000 = dot(g0000, Pf0);
  float n1000 = dot(g1000, vec4(Pf1.x, Pf0.yzw));
  float n0100 = dot(g0100, vec4(Pf0.x, Pf1.y, Pf0.zw));
  float n1100 = dot(g1100, vec4(Pf1.xy, Pf0.zw));
  float n0010 = dot(g0010, vec4(Pf0.xy, Pf1.z, Pf0.w));
  float n1010 = dot(g1010, vec4(Pf1.x, Pf0.y, Pf1.z, Pf0.w));
  float n0110 = dot(g0110, vec4(Pf0.x, Pf1.yz, Pf0.w));
  float n1110 = dot(g1110, vec4(Pf1.xyz, Pf0.w));
  float n0001 = dot(g0001, vec4(Pf0.xyz, Pf1.w));
  float n1001 = dot(g1001, vec4(Pf1.x, Pf0.yz, Pf1.w));
  float n0101 = dot(g0101, vec4(Pf0.x, Pf1.y, Pf0.z, Pf1.w));
  float n1101 = dot(g1101, vec4(Pf1.xy, Pf0.z, Pf1.w));
  float n0011 = dot(g0011, vec4(Pf0.xy, Pf1.zw));
  float n1011 = dot(g1011, vec4(Pf1.x, Pf0.y, Pf1.zw));
  float n0111 = dot(g0111, vec4(Pf0.x, Pf1.yzw));
  float n1111 = dot(g1111, Pf1);

  vec4 fade_xyzw = fade(Pf0);
  vec4 n_0w = mix(vec4(n0000, n1000, n0100, n1100), vec4(n0001, n1001, n0101, n1101), fade_xyzw.w);
  vec4 n_1w = mix(vec4(n0010, n1010, n0110, n1110), vec4(n0011, n1011, n0111, n1111), fade_xyzw.w);
  vec4 n_zw = mix(n_0w, n_1w, fade_xyzw.z);
  vec2 n_yzw = mix(n_zw.xy, n_zw.zw, fade_xyzw.y);
  float n_xyzw = mix(n_yzw.x, n_yzw.y, fade_xyzw.x);
  return 2.2 * n_xyzw;
}

float noise(vec3 p) {
    return 0.5 * cnoise(vec4(p, 0)) + 0.5;
}

float nebula(vec3 p) {
    const int steps = 6;
    float scale = pow(2.0, float(steps));
    vec3 displace;
    for (int i = 0; i < steps; i++) {
        displace = vec3(
            noise(p.xyz * scale + displace),
            noise(p.yzx * scale + displace),
            noise(p.zxy * scale + displace)
        );
        scale *= 0.5;
    }
    return noise(p * scale + displace);
}

void main() {
    vec3 posn = normalize(v_position) * uScale;
    float c = min(1.0, nebula(posn + uOffset) * uIntensity);
    c = pow(c, uFalloff);
    fragmentColor = vec4(uColor.xyz, c);
}
)";

// Sky bake stars: plain GL_POINTS in place of space-3d's geometry shader.
const char* kSkyStarBakeVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 inPos;
layout(location = 1) in float inBrightness;
layout(location = 2) in vec3 inColor;

uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform float pointSizePx;

out float vBrightness;
out vec3 vColor;

void main() {
    vBrightness = inBrightness;
    vColor = inColor;
    gl_Position = projectionMatrix * viewMatrix * vec4(inPos, 1.0);
    gl_PointSize = pointSizePx;
}
)";

// Port of SKYBOX_STARS_FRAG's cone falloff (pow(1-d, 0.5)) onto a plain
// point sprite: gl_PointCoord takes the geometry shader's v_coords place.
const char* kSkyStarBakeFragmentShaderSource = R"(#version 330 core
in float vBrightness;
in vec3 vColor;
out vec4 fragColor;

void main() {
    vec2 circCoord = 2.0 * gl_PointCoord - 1.0;
    float dist = pow(clamp(1.0 - length(circCoord), 0.0, 1.0), 0.5);
    float a = dist * vBrightness;
    fragColor = vec4(vColor * a, a);
}
)";

// Sky bake distant-galaxy smudges: soft anisotropic point sprites.
const char* kSkySmudgeBakeVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 inPos;
layout(location = 1) in float inAngle;
layout(location = 2) in float inPeak;
layout(location = 3) in vec3 inColor;

uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform float pointSizePx;

out float vAngle;
out float vPeak;
out vec3 vColor;

void main() {
    vAngle = inAngle;
    vPeak = inPeak;
    vColor = inColor;
    gl_Position = projectionMatrix * viewMatrix * vec4(inPos, 1.0);
    gl_PointSize = pointSizePx;
}
)";

// ~3:1 elliptical gaussian, rotated by the per-smudge angle.
const char* kSkySmudgeBakeFragmentShaderSource = R"(#version 330 core
in float vAngle;
in float vPeak;
in vec3 vColor;
out vec4 fragColor;

void main() {
    vec2 p = 2.0 * gl_PointCoord - 1.0;
    float c = cos(vAngle);
    float s = sin(vAngle);
    vec2 pr = vec2(p.x * c - p.y * s, p.x * s + p.y * c);
    // 3:1 aspect: the cross-axis term is 9x steeper.
    float g = exp(-(pr.x * pr.x + pr.y * pr.y * 9.0) * 2.0);
    float a = g * vPeak;
    fragColor = vec4(vColor * a, a);
}
)";

// Unit cube, 12 triangles, position only (from space-3d's Skybox.cpp).
constexpr float kSkyCubeVertices[108] = {
    -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f,
    1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f,

    -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f,
    -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,

    1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f,

    -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,

    -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f,

    -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f,
    1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f};

constexpr int kSkyCubemapSize = 1024;
constexpr quint32 kSkyStarTinyCount = 20000;
constexpr quint32 kSkyStarLargeCount = 100;
constexpr float kSkyStarTinyPointPx = 2.5f;
constexpr float kSkyStarLargePointPx = 10.0f;
// How far each near-white star is tinted toward the sector palette.
constexpr float kSkyStarPaletteTint = 0.15f;
constexpr int kSkySmudgeCount = 6;
constexpr float kSkySmudgePointPx = 48.0f;

// (seed, index, salt) -> [0,1), stateless so every bake element is reproducible.
float skyBakeHash01(quint64 seed, quint32 index, quint32 salt) {
    quint64 x = seed ^ (static_cast<quint64>(index) * 0x9E3779B97F4A7C15ULL) ^
                (static_cast<quint64>(salt) * 0xBF58476D1CE4E5B9ULL);
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return static_cast<float>(x >> 40) / static_cast<float>(1ULL << 24);
}

float skyBakeHashRange(quint64 seed, quint32 index, quint32 salt, float lo, float hi) {
    return lo + skyBakeHash01(seed, index, salt) * (hi - lo);
}

QVector3D lerp3(const QVector3D& a, const QVector3D& b, float t) {
    return a * (1.0f - t) + b * t;
}

const char* kBloomExtractFragmentShaderSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D sceneTex;

// Soft knee: a hard cutoff rings every bright core. Max-channel, not
// luminance, so saturated blue still blooms.
const float kThreshold = 0.55;
const float kKneeWidth = 0.20;

void main() {
    vec3 color = texture(sceneTex, vUv).rgb;
    float brightness = max(color.r, max(color.g, color.b));
    float weight = smoothstep(kThreshold - kKneeWidth, kThreshold + kKneeWidth, brightness);
    fragColor = vec4(color * weight, weight);
}
)";

const char* kBloomBlurFragmentShaderSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D sourceTex;
uniform vec2 texelSize;  // 1/width, 1/height of sourceTex
uniform vec2 direction;  // (1,0) horizontal pass, (0,1) vertical pass

void main() {
    // Separable 9-tap gaussian; run once per direction.
    const float w0 = 0.227027;
    const float w1 = 0.1945946;
    const float w2 = 0.1216216;
    const float w3 = 0.054054;
    const float w4 = 0.016216;

    vec2 step = texelSize * direction;
    vec4 sum = texture(sourceTex, vUv) * w0;
    sum += (texture(sourceTex, vUv + step) + texture(sourceTex, vUv - step)) * w1;
    sum += (texture(sourceTex, vUv + step * 2.0) + texture(sourceTex, vUv - step * 2.0)) * w2;
    sum += (texture(sourceTex, vUv + step * 3.0) + texture(sourceTex, vUv - step * 3.0)) * w3;
    sum += (texture(sourceTex, vUv + step * 4.0) + texture(sourceTex, vUv - step * 4.0)) * w4;
    fragColor = sum;
}
)";

const char* kBloomCompositeFragmentShaderSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D sceneTex;
uniform sampler2D bloomTex;
uniform float bloomStrength;

void main() {
    vec3 scene = texture(sceneTex, vUv).rgb;
    vec3 bloom = texture(bloomTex, vUv).rgb;
    fragColor = vec4(scene + bloomStrength * bloom, 1.0);
}
)";

}  // namespace

// Deterministic sky inputs for a sector: a hash picks a curated palette pair
// and becomes the bake seed.
GalaxyGlWidget::SkyParams GalaxyGlWidget::skyParamsFor(int sectorX, int sectorY) {
    auto mix64 = [](quint64 x) {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    };
    const auto ux = static_cast<quint64>(static_cast<quint32>(sectorX));
    const auto uy = static_cast<quint64>(static_cast<quint32>(sectorY));
    const quint64 h = mix64(ux ^ mix64(uy));

    static const QVector3D kPalettePairs[][2] = {
        {{0.42f, 0.18f, 0.55f}, {0.62f, 0.22f, 0.14f}},  // violet / ember
        {{0.10f, 0.42f, 0.46f}, {0.72f, 0.42f, 0.14f}},  // teal / amber
        {{0.14f, 0.22f, 0.58f}, {0.58f, 0.18f, 0.48f}},  // deep blue / magenta
        {{0.66f, 0.48f, 0.18f}, {0.14f, 0.44f, 0.52f}},  // gold / cyan
        {{0.10f, 0.44f, 0.32f}, {0.52f, 0.14f, 0.22f}},  // emerald / crimson
        {{0.24f, 0.18f, 0.55f}, {0.62f, 0.30f, 0.40f}},  // indigo / rose
    };
    constexpr int kPairCount = static_cast<int>(sizeof(kPalettePairs) / sizeof(kPalettePairs[0]));

    GalaxyGlWidget::SkyParams params;
    const int pair = static_cast<int>((h >> 32) % kPairCount);
    params.colorA = kPalettePairs[pair][0];
    params.colorB = kPalettePairs[pair][1];
    params.seed = h;
    return params;
}

GalaxyGlWidget::GalaxyGlWidget(QWidget* parent) : QOpenGLWidget(parent) {
    // Click focus: Space-to-reset works without stealing keys from the toolbar.
    setFocusPolicy(Qt::ClickFocus);
}

GalaxyGlWidget::~GalaxyGlWidget() {
    // All GL resources must be released while the context is current.
    makeCurrent();
    for (GpuGalaxy& galaxy : galaxies_) {
        galaxy.destroyGl();
    }
    for (GpuGalaxy& galaxy : departingGalaxies_) {
        galaxy.destroyGl();
    }
    for (GpuGalaxy& galaxy : deferredRelease_) {
        galaxy.destroyGl();
    }
    streaks_.destroyGl();
    voidQuadVao_.destroy();
    voidQuadVbo_.destroy();
    skyCubeVao_.destroy();
    skyCubeVbo_.destroy();
    if (skyCubemapCurrent_) glDeleteTextures(1, &skyCubemapCurrent_);
    if (skyCubemapDest_) glDeleteTextures(1, &skyCubemapDest_);
    if (skyBakeFbo_) glDeleteFramebuffers(1, &skyBakeFbo_);
    // FBOs must be reset before doneCurrent().
    sceneFbo_.reset();
    brightFbo_.reset();
    blurPingFbo_.reset();
    blurPongFbo_.reset();
    doneCurrent();
}

void GalaxyGlWidget::beginSector(int slotCount, bool reframeCamera, quint64 layoutSeed) {
    // The current board becomes the departing one; any older departing board is released.
    for (GpuGalaxy& galaxy : departingGalaxies_) {
        deferredRelease_.push_back(std::move(galaxy));
    }
    departingGalaxies_ = std::move(galaxies_);
    galaxies_.clear();

    // Queued uploads target the old layout's slots.
    pendingUploads_.clear();

    // The departing board keeps its own layout keys; slots are evaluated per frame.
    departingSlotCount_ = slotCount_;
    departingLayoutSeed_ = layoutSeed_;
    slotCount_ = slotCount;
    layoutSeed_ = layoutSeed;

    // Jump launches skip the reframe: the journey departs from the current view.
    if (reframeCamera) {
        // Frame the whole sector with a margin.
        const float outerRadius =
            kRadiusStep * std::sqrt(static_cast<float>(std::max(slotCount, 1))) + kGalaxyWorldRadius;
        distance_ = std::clamp(outerRadius * 2.2f, kMinDistance, kMaxDistance);
        azimuth_ = 35.0f;
        elevation_ = 25.0f;
        orbitTarget_ = sectorCenter();  // cursor-zoom drift resets with the rest of the pose
        targetDistance_ = distance_;    // snap the smooth-zoom end state along
        targetOrbit_ = orbitTarget_;
    }

    update();
}

void GalaxyGlWidget::setGalaxyCloud(std::uint64_t galaxyId, int slotIndex, const GalaxyCloudData& data) {
    pendingUploads_.push_back(PendingUpload{galaxyId, slotIndex, data});
    update();
}

void GalaxyGlWidget::setJumpVisual(JumpVisual v, float transit01) {
    if (v == JumpVisual::Jumping && jumpVisual_ != JumpVisual::Jumping) {
        // Fresh jump: freeze the axis to the current forward and snap the
        // smooth-zoom targets, or an in-flight ease would keep moving the camera.
        targetDistance_ = distance_;
        targetOrbit_ = orbitTarget_;
        jumpAxis_ = (orbitTarget_ - cameraEye()).normalized();
        // Deterministic per-jump sideways bow, perpendicular to the axis.
        ++jumpCurveSeed_;
        const float theta =
            static_cast<float>((jumpCurveSeed_ * 2654435761u) % 6283u) * 0.001f;
        QVector3D latRight = QVector3D::crossProduct(jumpAxis_, QVector3D(0.0f, 1.0f, 0.0f)).normalized();
        if (latRight.isNull()) {
            latRight = QVector3D(1.0f, 0.0f, 0.0f);
        }
        const QVector3D latUp = QVector3D::crossProduct(latRight, jumpAxis_);
        jumpLatDir_ = latRight * std::cos(theta) + latUp * std::sin(theta);
    }
    if (v == JumpVisual::Idle && jumpVisual_ == JumpVisual::Jumping) {
        for (GpuGalaxy& galaxy : departingGalaxies_) {
            deferredRelease_.push_back(std::move(galaxy));
        }
        departingGalaxies_.clear();
        // Fold the finished journey's scroll in so the field doesn't rewind.
        streakScroll_ += kStreakSweeps;
    }
    jumpVisual_ = v;
    transit01_ = std::clamp(transit01, 0.0f, 1.0f);

    // Smootherstep travel; speed is its derivative normalized to peak 1.0.
    const float t = transit01_;
    jumpTravel01_ = t * t * t * (t * (6.0f * t - 15.0f) + 10.0f);
    jumpSpeed_ = (jumpVisual_ == JumpVisual::Jumping)
                     ? 30.0f * t * t * (1.0f - t) * (1.0f - t) / 1.875f
                     : 0.0f;
    update();
}

void GalaxyGlWidget::setHudLines(const QString& line1, const QString& line2) {
    hudLine1_ = line1;
    hudLine2_ = line2;
    update();
}

void GalaxyGlWidget::setSectorSky(int currentX, int currentY, int destX, int destY) {
    const bool firstCall = !skySectorInitialized_;
    if (!firstCall && currentX == skyCurrentX_ && currentY == skyCurrentY_ && destX == skyDestX_ &&
        destY == skyDestY_) {
        return;  // called on every fact -- unchanged coordinates cost nothing
    }
    // firstCall forces both: (0,0) is the default and also a legal sector.
    if (firstCall || currentX != skyCurrentX_ || currentY != skyCurrentY_) {
        skyCurrentDirty_ = true;
    }
    if (firstCall || destX != skyDestX_ || destY != skyDestY_) {
        skyDestDirty_ = true;
    }
    skySectorInitialized_ = true;
    skyCurrentX_ = currentX;
    skyCurrentY_ = currentY;
    skyDestX_ = destX;
    skyDestY_ = destY;
    skyCurrent_ = skyParamsFor(currentX, currentY);
    skyDest_ = skyParamsFor(destX, destY);
    update();
}

void GalaxyGlWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // the sky pass paints over this every frame anyway
    glEnable(GL_PROGRAM_POINT_SIZE);

    skyProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kSkyVertexShaderSource);
    skyProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kSkyFragmentShaderSource);
    skyProgram_.link();

    starProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kStarVertexShaderSource);
    starProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kStarFragmentShaderSource);
    starProgram_.link();

    streakProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kStreakVertexShaderSource);
    streakProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kStreakFragmentShaderSource);
    streakProgram_.link();

    // Bloom programs reuse voidQuadVao_: position is attribute 0 in every one.
    bloomExtractProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kFullscreenVertexShaderSource);
    bloomExtractProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kBloomExtractFragmentShaderSource);
    bloomExtractProgram_.link();

    bloomBlurProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kFullscreenVertexShaderSource);
    bloomBlurProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kBloomBlurFragmentShaderSource);
    bloomBlurProgram_.link();

    bloomCompositeProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kFullscreenVertexShaderSource);
    bloomCompositeProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kBloomCompositeFragmentShaderSource);
    bloomCompositeProgram_.link();

    skyNebulaBakeProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kSkyNebulaBakeVertexShaderSource);
    skyNebulaBakeProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kSkyNebulaBakeFragmentShaderSource);
    skyNebulaBakeProgram_.link();

    skyStarBakeProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kSkyStarBakeVertexShaderSource);
    skyStarBakeProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kSkyStarBakeFragmentShaderSource);
    skyStarBakeProgram_.link();

    skySmudgeBakeProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kSkySmudgeBakeVertexShaderSource);
    skySmudgeBakeProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kSkySmudgeBakeFragmentShaderSource);
    skySmudgeBakeProgram_.link();

    // Fullscreen triangle shared by the sky and every bloom pass.
    static const float kBigTriangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    voidQuadVao_.create();
    voidQuadVao_.bind();
    voidQuadVbo_.create();
    voidQuadVbo_.bind();
    voidQuadVbo_.allocate(kBigTriangle, sizeof(kBigTriangle));
    skyProgram_.enableAttributeArray(0);
    skyProgram_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 0);
    voidQuadVao_.release();
    voidQuadVbo_.release();

    // Unit-cube VAO for the sky nebula bake.
    skyCubeVao_.create();
    skyCubeVao_.bind();
    skyCubeVbo_.create();
    skyCubeVbo_.bind();
    skyCubeVbo_.allocate(kSkyCubeVertices, sizeof(kSkyCubeVertices));
    skyNebulaBakeProgram_.enableAttributeArray(0);
    skyNebulaBakeProgram_.setAttributeBuffer(0, GL_FLOAT, 0, 3, 0);
    skyCubeVao_.release();
    skyCubeVbo_.release();
}

void GalaxyGlWidget::resizeGL(int w, int h) {
    aspect_ = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    projection_.setToIdentity();
    // 45-degree vertical FOV at rest; paintGL rebuilds this every frame.
    projection_.perspective(45.0f, aspect_, 0.05f, 1000.0f);

    // Best effort only: w/h may not be device pixels under High-DPI; paintGL corrects.
    const int pixelWidth = std::max(static_cast<int>(std::lround(w * devicePixelRatioF())), 1);
    const int pixelHeight = std::max(static_cast<int>(std::lround(h * devicePixelRatioF())), 1);
    ensureBloomTargets(pixelWidth, pixelHeight);
}

void GalaxyGlWidget::paintGL() {
    // The only place per-item GL objects are created or destroyed (context is
    // current). Release before upload so two sectors never coexist on the GPU.
    releaseDeferred();
    drainPendingUploads();
    if (streaksDirty_) {
        regenerateStreaks();
    }

    QPainter painter(this);
    painter.beginNativePainting();

    // Bake at most one dirty cubemap per frame; the current sector goes first.
    ensureSkyCubemaps();
    if (skyCurrentDirty_) {
        bakeSkyCubemap(skyCubemapCurrent_, skyCurrent_);
        skyCurrentDirty_ = false;
    } else if (skyDestDirty_) {
        bakeSkyCubemap(skyCubemapDest_, skyDest_);
        skyDestDirty_ = false;
    }

    // Size offscreen FBOs from the bound viewport, not width()*DPR (they can disagree).
    GLint viewportPx[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewportPx);
    const int pixelWidth =
        viewportPx[2] > 0 ? viewportPx[2] : std::max(static_cast<int>(std::lround(width() * devicePixelRatioF())), 1);
    const int pixelHeight = viewportPx[3] > 0
                                 ? viewportPx[3]
                                 : std::max(static_cast<int>(std::lround(height() * devicePixelRatioF())), 1);
    ensureBloomTargets(pixelWidth, pixelHeight);

    // ---- Pass 1: native scene into the float scene FBO. ---------------------
    sceneFbo_->bind();
    glViewport(0, 0, sceneFbo_->width(), sceneFbo_->height());

    // Reset GL state every frame: QPainter leaks state into the next frame.
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClearDepthf(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    // Depth clear is a no-op while glDepthMask is GL_FALSE.
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthMask(GL_FALSE);

    if (starProgram_.isLinked()) {
        const bool jumping = (jumpVisual_ == JumpVisual::Jumping);

        // Shared orbital clock; same rate as Galaxy-Renderer (1e5 years/frame at 60 fps).
        constexpr float kGalaxyTimeRate = 6.0e6f;
        float gdt = 0.0f;
        if (galaxyClock_.isValid()) {
            gdt = std::clamp(static_cast<float>(galaxyClock_.restart()) / 1000.0f, 0.0f, 0.1f);
        } else {
            galaxyClock_.start();
        }
        galaxyTime_ += gdt * kGalaxyTimeRate;

        // Smooth zoom: exponential ease toward the wheel's end state.
        const float zoomEase = 1.0f - std::exp(-gdt * 8.0f);
        distance_ += (targetDistance_ - distance_) * zoomEase;
        orbitTarget_ += (targetOrbit_ - orbitTarget_) * zoomEase;

        // FOV widens with journey speed; near/far planes scale with the dolly.
        projection_.setToIdentity();
        const float nearPlane = std::clamp(distance_ * 0.05f, 0.002f, 0.05f);
        const float farPlane = std::max(1500.0f, distance_ * 4.0f);
        projection_.perspective(45.0f + 18.0f * jumpSpeed_, aspect_, nearPlane, farPlane);

        QMatrix4x4 view;
        view.lookAt(cameraEye(), orbitTarget_, QVector3D(0.0f, 1.0f, 0.0f));

        drawSky();
        drawStreakLayer(view);

        const float travel = jumpTravel01_ * kJumpDistance;
        // Sideways bow: quartic ends keep launch and arrival straight.
        const float tj = transit01_;
        const float tj1 = 1.0f - tj;
        const float bump = 256.0f * tj * tj * tj * tj * tj1 * tj1 * tj1 * tj1;
        const QVector3D lateral = jumping ? jumpLatDir_ * (-kJumpCurve * bump) : QVector3D();
        const QVector3D destinationOffset =
            jumping ? jumpAxis_ * (kJumpDistance - travel) + lateral : QVector3D();
        const QVector3D departingOffset = jumpAxis_ * -travel + lateral;

        // One depth anchor shared by both boards: the nearest galaxy overall.
        float nearestDeparting = 3.4e38f;
        float nearestDestination = 3.4e38f;
        const QVector3D eyePos = cameraEye();
        if (jumping) {
            for (const GpuGalaxy& g : departingGalaxies_) {
                nearestDeparting = std::min(
                    nearestDeparting, (departingOffset + slotPosition(g.slotIndex, true) - eyePos).length());
            }
        }
        for (const GpuGalaxy& g : galaxies_) {
            nearestDestination =
                std::min(nearestDestination, (destinationOffset + slotPosition(g.slotIndex) - eyePos).length());
        }
        const float refDist = std::min(nearestDeparting, nearestDestination);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        if (jumping && !departingGalaxies_.empty()) {
            drawGalaxySet(departingGalaxies_, view, departingOffset, refDist, /*departing=*/true);
        }
        drawGalaxySet(galaxies_, view, destinationOffset, refDist, /*departing=*/false);
        glDisable(GL_BLEND);
    }

    sceneFbo_->release();

    // ---- Passes 2-4: quarter-res extract and ping/pong blur. ----------------
    drawBloomExtract();
    drawBloomBlurPass(brightFbo_->texture(), *blurPingFbo_, /*horizontal=*/true);
    drawBloomBlurPass(blurPingFbo_->texture(), *blurPongFbo_, /*horizontal=*/false);

    // ---- Pass 5: composite. QOpenGLWidget's target is never fbo 0. ---------
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, pixelWidth, pixelHeight);
    drawBloomComposite();

    painter.endNativePainting();

    if (!hudLine1_.isEmpty() || !hudLine2_.isEmpty()) {
        drawHud(painter);
    }
}

void GalaxyGlWidget::releaseDeferred() {
    for (GpuGalaxy& galaxy : deferredRelease_) {
        galaxy.destroyGl();
    }
    deferredRelease_.clear();
}

void GalaxyGlWidget::drainPendingUploads() {
    // A re-upload replaces the galaxy by id; the old one goes to deferredRelease_.
    for (const PendingUpload& pending : pendingUploads_) {
        const auto existing = std::find_if(galaxies_.begin(), galaxies_.end(), [&pending](const GpuGalaxy& g) {
            return g.galaxyId == pending.galaxyId;
        });
        if (existing != galaxies_.end()) {
            deferredRelease_.push_back(std::move(*existing));
            galaxies_.erase(existing);
        }
        galaxies_.push_back(uploadGalaxy(pending));
    }
    pendingUploads_.clear();
}

GalaxyGlWidget::GpuGalaxy GalaxyGlWidget::uploadGalaxy(const PendingUpload& pending) {
    const GalaxyCloudData& data = pending.data;
    const std::size_t pointCount = data.orbitA.size();
    const bool hasKinds = data.kinds.size() == pointCount;

    // 9 floats per point: five orbit scalars, rgb, mag. The shader derives position.
    std::vector<float> interleaved(pointCount * 9);
    for (std::size_t s = 0; s < pointCount; ++s) {
        float* out = &interleaved[s * 9];
        out[0] = data.orbitA[s];
        out[1] = data.orbitB[s];
        out[2] = data.theta0[s];
        out[3] = data.velTheta[s];
        out[4] = data.tiltAngle[s];
        out[5] = data.colors[s * 3 + 0];
        out[6] = data.colors[s * 3 + 1];
        out[7] = data.colors[s * 3 + 2];
        out[8] = data.mags[s];
    }

    GpuGalaxy galaxy;
    galaxy.vao = std::make_unique<QOpenGLVertexArrayObject>();
    galaxy.vao->create();
    galaxy.vao->bind();

    galaxy.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    galaxy.vbo->create();
    galaxy.vbo->bind();
    galaxy.vbo->allocate(interleaved.data(), static_cast<int>(interleaved.size() * sizeof(float)));

    constexpr int kStride = static_cast<int>(9 * sizeof(float));
    for (int attr = 0; attr < 5; ++attr) {  // orbitA/orbitB/theta0/velTheta/tilt
        starProgram_.enableAttributeArray(attr);
        starProgram_.setAttributeBuffer(attr, GL_FLOAT, static_cast<int>(attr * sizeof(float)), 1, kStride);
    }
    starProgram_.enableAttributeArray(5);
    starProgram_.setAttributeBuffer(5, GL_FLOAT, static_cast<int>(5 * sizeof(float)), 3, kStride);
    starProgram_.enableAttributeArray(6);
    starProgram_.setAttributeBuffer(6, GL_FLOAT, static_cast<int>(8 * sizeof(float)), 1, kStride);

    galaxy.vao->release();
    galaxy.vbo->release();

    galaxy.starCount = static_cast<int>(pointCount);
    galaxy.slotIndex = pending.slotIndex;
    galaxy.galaxyId = pending.galaxyId;
    galaxy.params = data.params;

    // Points are kind-sorted; counting per kind keeps a malformed buffer in range.
    if (hasKinds) {
        for (std::size_t s = 0; s < pointCount; ++s) {
            const int kind = std::min<int>(data.kinds[s], kKindCount - 1);
            ++galaxy.kindCount[kind];
        }
        int first = 0;
        for (int kind = 0; kind < kKindCount; ++kind) {
            galaxy.kindFirst[kind] = first;
            first += galaxy.kindCount[kind];
        }
    } else {
        galaxy.kindFirst[kKindStar] = 0;
        galaxy.kindCount[kKindStar] = static_cast<int>(pointCount);
    }
    return galaxy;
}

void GalaxyGlWidget::regenerateStreaks() {
    streaks_.destroyGl();

    std::mt19937 rng(static_cast<unsigned>(streakSeed_));
    // Uniform cross-section offsets and axial phase around the camera.
    std::uniform_real_distribution<float> crossDist(-kStreakFieldRadius, kStreakFieldRadius);
    std::uniform_real_distribution<float> axialPhaseDist(0.0f, 1.0f);
    std::uniform_int_distribution<int> colorPick(0, 2);

    // Gold / blue-white / purple, matching galaxylib's star palette.
    const float kPalette[3][3] = {
        {1.00f, 0.80f, 0.40f},
        {0.55f, 0.75f, 1.00f},
        {0.70f, 0.50f, 1.00f},
    };

    // Six vertices per lane: (u, v, a, t, side, rgb); the vertex shader does the rest.
    std::vector<float> verts;
    verts.reserve(static_cast<std::size_t>(kStreakLaneCount) * 6 * 8);

    for (int lane = 0; lane < kStreakLaneCount; ++lane) {
        const float u = crossDist(rng);
        const float v = crossDist(rng);
        const float a = axialPhaseDist(rng);
        const float* color = kPalette[colorPick(rng)];

        auto pushVertex = [&](float t, float side) {
            verts.insert(verts.end(), {u, v, a, t, side, color[0], color[1], color[2]});
        };
        pushVertex(0.0f, -1.0f);
        pushVertex(0.0f, 1.0f);
        pushVertex(1.0f, 1.0f);
        pushVertex(0.0f, -1.0f);
        pushVertex(1.0f, 1.0f);
        pushVertex(1.0f, -1.0f);
    }

    streaks_.vao = std::make_unique<QOpenGLVertexArrayObject>();
    streaks_.vao->create();
    streaks_.vao->bind();

    streaks_.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    streaks_.vbo->create();
    streaks_.vbo->bind();
    streaks_.vbo->allocate(verts.data(), static_cast<int>(verts.size() * sizeof(float)));

    constexpr int kStride = static_cast<int>(8 * sizeof(float));
    streakProgram_.enableAttributeArray(0);
    streakProgram_.setAttributeBuffer(0, GL_FLOAT, 0, 3, kStride);
    streakProgram_.enableAttributeArray(1);
    streakProgram_.setAttributeBuffer(1, GL_FLOAT, static_cast<int>(3 * sizeof(float)), 1, kStride);
    streakProgram_.enableAttributeArray(2);
    streakProgram_.setAttributeBuffer(2, GL_FLOAT, static_cast<int>(4 * sizeof(float)), 1, kStride);
    streakProgram_.enableAttributeArray(3);
    streakProgram_.setAttributeBuffer(3, GL_FLOAT, static_cast<int>(5 * sizeof(float)), 3, kStride);

    streaks_.vao->release();
    streaks_.vbo->release();

    streaks_.vertexCount = static_cast<int>(verts.size() / 8);
    streaksDirty_ = false;
}

void GalaxyGlWidget::drawSky() {
    if (!skyProgram_.isLinked()) {
        return;
    }

    const QVector3D eye = cameraEye();
    // Must use the view matrix's target, or the sky slides against the geometry.
    QVector3D forward = (orbitTarget_ - eye).normalized();
    QVector3D right = QVector3D::crossProduct(forward, QVector3D(0.0f, 1.0f, 0.0f)).normalized();
    if (right.isNull()) {
        right = QVector3D(1.0f, 0.0f, 0.0f);  // looking straight up/down -- any horizontal right works
    }
    const QVector3D up = QVector3D::crossProduct(right, forward);

    glDisable(GL_BLEND);  // opaque background: replaces the clear, everything else blends on top
    skyProgram_.bind();
    skyProgram_.setUniformValue("camRight", right);
    skyProgram_.setUniformValue("camUp", up);
    skyProgram_.setUniformValue("camForward", forward);
    // Must match paintGL's projection FOV.
    skyProgram_.setUniformValue("tanHalfFov",
                                 std::tan(qDegreesToRadians(0.5f * (45.0f + 18.0f * jumpSpeed_))));
    skyProgram_.setUniformValue("aspect", aspect_);
    skyProgram_.setUniformValue("jumpMix", jumpSpeed_);
    // Destination sky fades in over 25%..80% of travel.
    skyProgram_.setUniformValue(
        "skyBlend", jumpVisual_ == JumpVisual::Jumping
                        ? std::clamp((jumpTravel01_ - 0.25f) / 0.55f, 0.0f, 1.0f)
                        : 0.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyCubemapCurrent_);
    skyProgram_.setUniformValue("skyCurrent", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyCubemapDest_);
    skyProgram_.setUniformValue("skyDest", 1);

    voidQuadVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    voidQuadVao_.release();
    skyProgram_.release();
}

void GalaxyGlWidget::ensureSkyCubemaps() {
    if (skyCubemapsReady_) {
        return;
    }
    glGenTextures(1, &skyCubemapCurrent_);
    glGenTextures(1, &skyCubemapDest_);
    for (GLuint tex : {skyCubemapCurrent_, skyCubemapDest_}) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
        for (int face = 0; face < 6; ++face) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB8, kSkyCubemapSize, kSkyCubemapSize, 0,
                         GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        }
        // LINEAR until the first bake builds mipmaps: a mipmap filter now reads black.
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glGenFramebuffers(1, &skyBakeFbo_);
    skyCubemapsReady_ = true;
}

// Bakes stars, nebulae and smudges into cubemapTex. Restores all GL state it touches.
void GalaxyGlWidget::bakeSkyCubemap(GLuint cubemapTex, const SkyParams& params) {
    GLint prevFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    GLint prevBlendSrc = GL_ONE;
    GLint prevBlendDst = GL_ZERO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, skyBakeFbo_);
    glViewport(0, 0, kSkyCubemapSize, kSkyCubemapSize);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive, as in space-3d

    for (int face = 0; face < 6; ++face) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                cubemapTex, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // One outward-looking view per cube face, from the origin.
    QMatrix4x4 views[6];
    views[0].lookAt(QVector3D(0, 0, 0), QVector3D(1, 0, 0), QVector3D(0, -1, 0));
    views[1].lookAt(QVector3D(0, 0, 0), QVector3D(-1, 0, 0), QVector3D(0, -1, 0));
    views[2].lookAt(QVector3D(0, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1));
    views[3].lookAt(QVector3D(0, 0, 0), QVector3D(0, -1, 0), QVector3D(0, 0, -1));
    views[4].lookAt(QVector3D(0, 0, 0), QVector3D(0, 0, 1), QVector3D(0, -1, 0));
    views[5].lookAt(QVector3D(0, 0, 0), QVector3D(0, 0, -1), QVector3D(0, -1, 0));
    QMatrix4x4 projection;
    projection.perspective(90.0f, 1.0f, 0.1f, 1000.0f);

    bakeSkyStars(views, projection, cubemapTex, params);
    bakeSkyNebulae(views, projection, cubemapTex, params);
    bakeSkySmudges(views, projection, cubemapTex, params);

    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTex);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevBlend) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    glBlendFunc(static_cast<GLenum>(prevBlendSrc), static_cast<GLenum>(prevBlendDst));
    if (prevDepth) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

// Two passes, tiny and large stars, each with its own point size.
void GalaxyGlWidget::bakeSkyStars(const QMatrix4x4 views[6], const QMatrix4x4& projection, GLuint cubemapTex,
                                    const SkyParams& params) {
    if (!skyStarBakeProgram_.isLinked()) {
        return;
    }

    struct StarPass {
        quint32 count;
        quint32 idxBase;
        float pointSizePx;
    };
    const StarPass passes[2] = {
        {kSkyStarTinyCount, 0u, kSkyStarTinyPointPx},
        {kSkyStarLargeCount, kSkyStarTinyCount, kSkyStarLargePointPx},
    };

    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo(QOpenGLBuffer::VertexBuffer);
    vao.create();
    vao.bind();
    vbo.create();
    vbo.bind();

    skyStarBakeProgram_.bind();
    skyStarBakeProgram_.setUniformValue("projectionMatrix", projection);

    for (const StarPass& pass : passes) {
        std::vector<float> verts;
        verts.reserve(pass.count * 7u);
        for (quint32 i = 0; i < pass.count; ++i) {
            const quint32 idx = pass.idxBase + i;
            QVector3D dir(skyBakeHashRange(params.seed, idx, 0, -1.0f, 1.0f),
                          skyBakeHashRange(params.seed, idx, 1, -1.0f, 1.0f),
                          skyBakeHashRange(params.seed, idx, 2, -1.0f, 1.0f));
            dir = dir.normalized() * 100.0f;
            const QVector3D white(skyBakeHashRange(params.seed, idx, 3, 0.9f, 1.0f),
                                   skyBakeHashRange(params.seed, idx, 4, 0.9f, 1.0f),
                                   skyBakeHashRange(params.seed, idx, 5, 0.9f, 1.0f));
            const QVector3D palette = lerp3(params.colorA, params.colorB, skyBakeHash01(params.seed, idx, 6));
            const QVector3D color = lerp3(white, palette, kSkyStarPaletteTint);
            const float brightness = skyBakeHashRange(params.seed, idx, 7, 0.7f, 1.0f);
            verts.push_back(dir.x());
            verts.push_back(dir.y());
            verts.push_back(dir.z());
            verts.push_back(brightness);
            verts.push_back(color.x());
            verts.push_back(color.y());
            verts.push_back(color.z());
        }
        vbo.allocate(verts.data(), static_cast<int>(verts.size() * sizeof(float)));
        skyStarBakeProgram_.enableAttributeArray(0);
        skyStarBakeProgram_.setAttributeBuffer(0, GL_FLOAT, 0, 3, 7 * sizeof(float));
        skyStarBakeProgram_.enableAttributeArray(1);
        skyStarBakeProgram_.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 1, 7 * sizeof(float));
        skyStarBakeProgram_.enableAttributeArray(2);
        skyStarBakeProgram_.setAttributeBuffer(2, GL_FLOAT, 4 * sizeof(float), 3, 7 * sizeof(float));
        skyStarBakeProgram_.setUniformValue("pointSizePx", pass.pointSizePx);

        for (int face = 0; face < 6; ++face) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                    cubemapTex, 0);
            skyStarBakeProgram_.setUniformValue("viewMatrix", views[face]);
            glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(pass.count));
        }
    }

    skyStarBakeProgram_.release();
    vbo.destroy();
    vao.destroy();
}

// 3-5 nebula layers, colored from the sector palette.
void GalaxyGlWidget::bakeSkyNebulae(const QMatrix4x4 views[6], const QMatrix4x4& projection, GLuint cubemapTex,
                                      const SkyParams& params) {
    if (!skyNebulaBakeProgram_.isLinked()) {
        return;
    }

    const int layerCount = 3 + static_cast<int>((params.seed >> 40) % 3ULL);

    skyNebulaBakeProgram_.bind();
    skyNebulaBakeProgram_.setUniformValue("projectionMatrix", projection);
    skyCubeVao_.bind();

    for (int layer = 0; layer < layerCount; ++layer) {
        const quint32 idx = static_cast<quint32>(layer);
        const float scale = skyBakeHashRange(params.seed, idx, 10, 0.25f, 0.75f);
        const float intensity = skyBakeHashRange(params.seed, idx, 11, 0.9f, 1.1f);
        const float falloff = skyBakeHashRange(params.seed, idx, 12, 3.0f, 6.0f);
        const QVector3D offset(skyBakeHashRange(params.seed, idx, 13, -1000.0f, 1000.0f),
                                skyBakeHashRange(params.seed, idx, 14, -1000.0f, 1000.0f),
                                skyBakeHashRange(params.seed, idx, 15, -1000.0f, 1000.0f));
        const QVector3D color = lerp3(params.colorA, params.colorB, skyBakeHash01(params.seed, idx, 16));

        skyNebulaBakeProgram_.setUniformValue("uScale", scale);
        skyNebulaBakeProgram_.setUniformValue("uIntensity", intensity);
        skyNebulaBakeProgram_.setUniformValue("uFalloff", falloff);
        skyNebulaBakeProgram_.setUniformValue("uOffset", offset);
        skyNebulaBakeProgram_.setUniformValue("uColor", color.x(), color.y(), color.z(), 1.0f);

        for (int face = 0; face < 6; ++face) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                    cubemapTex, 0);
            skyNebulaBakeProgram_.setUniformValue("viewMatrix", views[face]);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }

    skyCubeVao_.release();
    skyNebulaBakeProgram_.release();
}

void GalaxyGlWidget::bakeSkySmudges(const QMatrix4x4 views[6], const QMatrix4x4& projection, GLuint cubemapTex,
                                      const SkyParams& params) {
    if (!skySmudgeBakeProgram_.isLinked()) {
        return;
    }

    std::vector<float> verts;
    verts.reserve(kSkySmudgeCount * 8u);
    for (int i = 0; i < kSkySmudgeCount; ++i) {
        const quint32 idx = static_cast<quint32>(i);
        QVector3D dir(skyBakeHashRange(params.seed, idx, 20, -1.0f, 1.0f),
                      skyBakeHashRange(params.seed, idx, 21, -1.0f, 1.0f),
                      skyBakeHashRange(params.seed, idx, 22, -1.0f, 1.0f));
        dir = dir.normalized() * 100.0f;
        const float angle = skyBakeHashRange(params.seed, idx, 23, 0.0f, 6.2831853f);
        const float peak = skyBakeHashRange(params.seed, idx, 24, 0.10f, 0.20f);
        const QVector3D color = lerp3(params.colorA, params.colorB, skyBakeHash01(params.seed, idx, 25));
        verts.push_back(dir.x());
        verts.push_back(dir.y());
        verts.push_back(dir.z());
        verts.push_back(angle);
        verts.push_back(peak);
        verts.push_back(color.x());
        verts.push_back(color.y());
        verts.push_back(color.z());
    }

    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo(QOpenGLBuffer::VertexBuffer);
    vao.create();
    vao.bind();
    vbo.create();
    vbo.bind();
    vbo.allocate(verts.data(), static_cast<int>(verts.size() * sizeof(float)));

    skySmudgeBakeProgram_.bind();
    skySmudgeBakeProgram_.enableAttributeArray(0);
    skySmudgeBakeProgram_.setAttributeBuffer(0, GL_FLOAT, 0, 3, 8 * sizeof(float));
    skySmudgeBakeProgram_.enableAttributeArray(1);
    skySmudgeBakeProgram_.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 1, 8 * sizeof(float));
    skySmudgeBakeProgram_.enableAttributeArray(2);
    skySmudgeBakeProgram_.setAttributeBuffer(2, GL_FLOAT, 4 * sizeof(float), 1, 8 * sizeof(float));
    skySmudgeBakeProgram_.enableAttributeArray(3);
    skySmudgeBakeProgram_.setAttributeBuffer(3, GL_FLOAT, 5 * sizeof(float), 3, 8 * sizeof(float));
    skySmudgeBakeProgram_.setUniformValue("projectionMatrix", projection);
    skySmudgeBakeProgram_.setUniformValue("pointSizePx", kSkySmudgePointPx);

    for (int face = 0; face < 6; ++face) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                cubemapTex, 0);
        skySmudgeBakeProgram_.setUniformValue("viewMatrix", views[face]);
        glDrawArrays(GL_POINTS, 0, kSkySmudgeCount);
    }

    skySmudgeBakeProgram_.release();
    vbo.destroy();
    vao.destroy();
}

void GalaxyGlWidget::drawOneGalaxy(const GpuGalaxy& galaxy, const QMatrix4x4& view,
                                    const QVector3D& worldOffset, float refDist, bool departing) {
    // Count-invariant brightness, normalized to Galaxy-Renderer's 40000 stars.
    constexpr float kReferenceStars = 40000.0f;
    const int starCount = galaxy.kindCount[kKindStar];
    const float norm = starCount > 0 ? std::min(1.0f, kReferenceStars / static_cast<float>(starCount)) : 1.0f;

    // Depth is measured from the nearest galaxy over the sector's span, so the
    // gradient survives every zoom level.
    const float span =
        1.2f * (kRadiusStep * std::sqrt(static_cast<float>(std::max(slotCount_, 1))) + kGalaxyWorldRadius);
    const float dist =
        std::max((worldOffset + slotPosition(galaxy.slotIndex, departing) - cameraEye()).length(), 0.5f);
    const float anchor = std::clamp(refDist, 0.5f, dist);
    // One normalized depth for dimming, defocus, and desaturation.
    const float depth01 = std::clamp((dist - anchor) / std::max(span, 1.0f), 0.0f, 1.0f);

    const float worldR = kGalaxyWorldRadius * slotScale(galaxy.slotIndex, departing);
    QMatrix4x4 model;
    model.translate(worldOffset + slotPosition(galaxy.slotIndex, departing));
    model.rotate(slotYaw(galaxy.slotIndex, departing), 0.0f, 1.0f, 0.0f);
    model.rotate(slotPitch(galaxy.slotIndex, departing), 1.0f, 0.0f, 0.0f);
    model.scale(worldR);

    // Galaxy-Renderer's pixel sizes assume a galaxy radius of ~0.383 of a
    // 1080 px window; sizeFactor rescales them to this galaxy's projected radius.
    const float fbH = sceneFbo_ ? static_cast<float>(sceneFbo_->height())
                                : static_cast<float>(std::max(height(), 1));
    const float projectedRadiusPx = projection_(1, 1) * 0.5f * fbH * worldR / dist;
    // Capped at 1.0: closer than that framing, zoom only magnifies.
    const float sizeFactor = std::min(projectedRadiusPx / (0.383f * 1080.0f), 1.0f);

    // Brightness is already zoom-invariant (vEnergy covers sub-pixel sprites).
    const float common = norm * std::max(1.0f - 0.55f * depth01, 0.15f);

    starProgram_.bind();
    starProgram_.setUniformValue("mvp", projection_ * view * model);
    starProgram_.setUniformValue("defocus", depth01);
    starProgram_.setUniformValue("time", galaxyTime_);
    starProgram_.setUniformValue("sizeFactor", sizeFactor);
    const GalaxyRenderParams& p = galaxy.params;
    starProgram_.setUniformValue("radCoreN", p.radCoreN);
    starProgram_.setUniformValue("radFarFieldN", p.radFarFieldN);
    starProgram_.setUniformValue("exInner", p.exInner);
    starProgram_.setUniformValue("exOuter", p.exOuter);
    starProgram_.setUniformValue("angleOffsetN", p.angleOffsetN);
    starProgram_.setUniformValue("barRadiusN", p.barRadiusN);
    starProgram_.setUniformValue("barEx", p.barEx);
    starProgram_.setUniformValue("pertN", p.pertN);
    starProgram_.setUniformValue("pertAmp", p.pertAmp);
    starProgram_.setUniformValue("dustSize", p.dustRenderSize);
    starProgram_.setUniformValue("h2SizeMax", p.h2SizeMax);
    starProgram_.setUniformValue("h2Threshold", p.h2Threshold);
    galaxy.vao->bind();

    const auto pass = [&](int kind) {
        if (galaxy.kindCount[kind] <= 0) {
            return;
        }
        starProgram_.setUniformValue("kindMode", kind);
        starProgram_.setUniformValue("intensity", common);
        glDrawArrays(GL_POINTS, galaxy.kindFirst[kind], galaxy.kindCount[kind]);
    };
    // Purely additive: every population is emission, so draw order is irrelevant.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    pass(kKindStar);
    pass(kKindDust);
    pass(kKindFilament);
    pass(kKindH2);
    pass(kKindH2Core);

    galaxy.vao->release();
    starProgram_.release();
}

void GalaxyGlWidget::drawGalaxySet(const std::vector<GpuGalaxy>& set, const QMatrix4x4& view,
                                    const QVector3D& worldOffset, float refDist, bool departing) {
    // Additive, so no sorting is needed.
    for (const GpuGalaxy& galaxy : set) {
        drawOneGalaxy(galaxy, view, worldOffset, refDist, departing);
    }
}

void GalaxyGlWidget::drawStreakLayer(const QMatrix4x4& view) {
    if (!streaks_.vao || streaks_.vertexCount == 0) {
        return;
    }
    // Slow drift runs always; dt is clamped so a stall can't jump the field.
    float dt = 0.0f;
    if (streakClock_.isValid()) {
        dt = std::clamp(static_cast<float>(streakClock_.restart()) / 1000.0f, 0.0f, 0.1f);
    } else {
        streakClock_.start();
    }
    streakScroll_ += dt * kStreakDriftPerSec;
    // Ease the streak axis toward the curved path's tangent.
    QVector3D flowTarget = jumpAxis_;
    if (jumpVisual_ == JumpVisual::Jumping) {
        const float t = transit01_;
        const float t1 = 1.0f - t;
        const float axialRate = 30.0f * t * t * t1 * t1 * kJumpDistance;
        // Derivative of the bump 256 t^4 (1-t)^4.
        const float lateralRate = 1024.0f * t * t * t * t1 * t1 * t1 * (1.0f - 2.0f * t) * kJumpCurve;
        const QVector3D tangent = jumpAxis_ * axialRate + jumpLatDir_ * lateralRate;
        if (tangent.lengthSquared() > 1e-4f) {
            flowTarget = tangent.normalized();
        }
    }
    streakAxis_ = (streakAxis_ + (flowTarget - streakAxis_) * std::min(1.0f, dt * 4.0f)).normalized();
    if (streakAxis_.isNull()) {
        streakAxis_ = jumpAxis_;  // exact opposites mid-ease -- snap rather than divide by zero
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    streakProgram_.bind();
    QVector3D right = QVector3D::crossProduct(streakAxis_, QVector3D(0.0f, 1.0f, 0.0f)).normalized();
    if (right.isNull()) {
        right = QVector3D(1.0f, 0.0f, 0.0f);  // axis straight up/down -- any horizontal right works
    }
    const QVector3D up = QVector3D::crossProduct(right, streakAxis_);
    // Lens axis: camera -> destination board's current center (same math as paintGL).
    QVector3D lensDir = streakAxis_;
    float lensTan = 0.0f;
    if (jumpVisual_ == JumpVisual::Jumping) {
        const float tj = transit01_;
        const float tj1 = 1.0f - tj;
        const float bump = 256.0f * tj * tj * tj * tj * tj1 * tj1 * tj1 * tj1;
        const QVector3D destCenter = jumpAxis_ * (kJumpDistance - jumpTravel01_ * kJumpDistance) +
                                     jumpLatDir_ * (-kJumpCurve * bump);
        const QVector3D toDest = destCenter - cameraEye();
        const float destDist = toDest.length();
        if (destDist > 1e-2f) {
            lensDir = toDest / destDist;
        }
        // Cone = destination's angular footprint, gated 2.5x faster than speed.
        const float outerRadius =
            kRadiusStep * std::sqrt(static_cast<float>(std::max(slotCount_, 1))) + kGalaxyWorldRadius;
        lensTan = std::min(1.15f * outerRadius / std::max(destDist, 1.0f), 1.2f) *
                  std::min(jumpSpeed_ * 2.5f, 1.0f);
    }

    streakProgram_.setUniformValue("viewProj", projection_ * view);
    streakProgram_.setUniformValue("camEye", cameraEye());
    streakProgram_.setUniformValue("axisFwd", streakAxis_);
    streakProgram_.setUniformValue("axisRight", right);
    streakProgram_.setUniformValue("axisUp", up);
    streakProgram_.setUniformValue("lensDir", lensDir);
    streakProgram_.setUniformValue("lensTan", lensTan);
    streakProgram_.setUniformValue("scroll", streakScroll_ + jumpTravel01_ * kStreakSweeps);
    streakProgram_.setUniformValue("speed01", jumpSpeed_);
    streakProgram_.setUniformValue("aspect", aspect_);
    streaks_.vao->bind();
    glDrawArrays(GL_TRIANGLES, 0, streaks_.vertexCount);
    streaks_.vao->release();
    streakProgram_.release();
    glDisable(GL_BLEND);

    // The drift never stops, so keep repainting.
    update();
}

void GalaxyGlWidget::drawHud(QPainter& painter) {
    painter.setRenderHint(QPainter::Antialiasing, true);

    QFont font(QStringLiteral("Consolas"), 11);
    font.setStyleHint(QFont::Monospace);
    painter.setFont(font);
    const QFontMetrics metrics(font);

    const int lineHeight = metrics.height();
    const int textWidth = std::max(metrics.horizontalAdvance(hudLine1_), metrics.horizontalAdvance(hudLine2_));

    constexpr int kMargin = 24;
    constexpr int kPad = 14;
    constexpr int kBracketArm = 10;

    const int boxWidth = textWidth + kPad * 2;
    const int boxHeight = lineHeight * 2 + kPad * 2;
    const QRect box(width() - kMargin - boxWidth, kMargin, boxWidth, boxHeight);

    const QColor cyan(80, 220, 235);

    // Corner brackets only, no full outline.
    painter.setPen(QPen(cyan, 1.5));
    auto corner = [&](const QPoint& origin, QPoint armX, QPoint armY) {
        painter.drawLine(origin, origin + armX);
        painter.drawLine(origin, origin + armY);
    };
    corner(box.topLeft(), QPoint(kBracketArm, 0), QPoint(0, kBracketArm));
    corner(box.topRight(), QPoint(-kBracketArm, 0), QPoint(0, kBracketArm));
    corner(box.bottomLeft(), QPoint(kBracketArm, 0), QPoint(0, -kBracketArm));
    corner(box.bottomRight(), QPoint(-kBracketArm, 0), QPoint(0, -kBracketArm));

    painter.setPen(cyan);
    painter.drawText(QRect(box.left() + kPad, box.top() + kPad, textWidth, lineHeight), Qt::AlignLeft,
                      hudLine1_);
    painter.drawText(QRect(box.left() + kPad, box.top() + kPad + lineHeight, textWidth, lineHeight),
                      Qt::AlignLeft, hudLine2_);
}

void GalaxyGlWidget::ensureBloomTargets(int pixelWidth, int pixelHeight) {
    pixelWidth = std::max(pixelWidth, 1);
    pixelHeight = std::max(pixelHeight, 1);
    if (sceneFbo_ && pixelWidth == bloomFboWidth_ && pixelHeight == bloomFboHeight_) {
        return;  // already the right size -- the common case, one cheap comparison per frame
    }
    bloomFboWidth_ = pixelWidth;
    bloomFboHeight_ = pixelHeight;

    // RGBA16F so additive values above 1.0 survive; depth attached for the scene pass.
    QOpenGLFramebufferObjectFormat sceneFormat;
    sceneFormat.setInternalTextureFormat(GL_RGBA16F);
    sceneFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    sceneFbo_ = std::make_unique<QOpenGLFramebufferObject>(pixelWidth, pixelHeight, sceneFormat);

    // Extract and blur at quarter resolution, also HDR.
    const int bloomW = std::max(pixelWidth / 4, 1);
    const int bloomH = std::max(pixelHeight / 4, 1);
    QOpenGLFramebufferObjectFormat bloomFormat;
    bloomFormat.setInternalTextureFormat(GL_RGBA16F);
    brightFbo_ = std::make_unique<QOpenGLFramebufferObject>(bloomW, bloomH, bloomFormat);
    blurPingFbo_ = std::make_unique<QOpenGLFramebufferObject>(bloomW, bloomH, bloomFormat);
    blurPongFbo_ = std::make_unique<QOpenGLFramebufferObject>(bloomW, bloomH, bloomFormat);

    // Qt FBO textures default to GL_NEAREST, which grids the upsampled bloom.
    const auto makeFilterable = [this](GLuint tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    };
    makeFilterable(sceneFbo_->texture());
    makeFilterable(brightFbo_->texture());
    makeFilterable(blurPingFbo_->texture());
    makeFilterable(blurPongFbo_->texture());
}

void GalaxyGlWidget::drawBloomExtract() {
    if (!bloomExtractProgram_.isLinked()) {
        return;
    }
    brightFbo_->bind();
    glViewport(0, 0, brightFbo_->width(), brightFbo_->height());
    // Each post-process pass fully overwrites its target.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    bloomExtractProgram_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneFbo_->texture());
    bloomExtractProgram_.setUniformValue("sceneTex", 0);

    voidQuadVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    voidQuadVao_.release();

    bloomExtractProgram_.release();
    brightFbo_->release();
}

void GalaxyGlWidget::drawBloomBlurPass(GLuint sourceTex, QOpenGLFramebufferObject& target, bool horizontal) {
    if (!bloomBlurProgram_.isLinked()) {
        return;
    }
    target.bind();
    glViewport(0, 0, target.width(), target.height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    bloomBlurProgram_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);
    bloomBlurProgram_.setUniformValue("sourceTex", 0);
    // Source and target share the same size.
    bloomBlurProgram_.setUniformValue("texelSize",
                                       QVector2D(1.0f / static_cast<float>(target.width()),
                                                 1.0f / static_cast<float>(target.height())));
    bloomBlurProgram_.setUniformValue("direction", horizontal ? QVector2D(1.0f, 0.0f) : QVector2D(0.0f, 1.0f));

    voidQuadVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    voidQuadVao_.release();

    bloomBlurProgram_.release();
    target.release();
}

void GalaxyGlWidget::drawBloomComposite() {
    if (!bloomCompositeProgram_.isLinked()) {
        return;
    }
    // Draws into the framebuffer the caller bound.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    bloomCompositeProgram_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneFbo_->texture());
    bloomCompositeProgram_.setUniformValue("sceneTex", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, blurPongFbo_->texture());
    bloomCompositeProgram_.setUniformValue("bloomTex", 1);
    // GF_BLOOM_STRENGTH overrides the default; 0 disables bloom.
    static const float bloomStrength = [] {
        bool ok = false;
        const float v = qEnvironmentVariable("GF_BLOOM_STRENGTH").toFloat(&ok);
        return ok ? v : kBloomStrength;
    }();
    bloomCompositeProgram_.setUniformValue("bloomStrength", bloomStrength);

    voidQuadVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    voidQuadVao_.release();

    bloomCompositeProgram_.release();
}

namespace {
// Deterministic per-(sector, slot, salt) hash in [0, 1); the layout's only randomness.
float layoutHash01(quint64 layoutSeed, int slotIndex, quint32 salt) {
    quint64 x = layoutSeed ^ (static_cast<quint64>(static_cast<quint32>(slotIndex)) * 0x9E3779B97F4A7C15ULL) ^
                (static_cast<quint64>(salt) * 0xBF58476D1CE4E5B9ULL);
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return static_cast<float>(x >> 40) / static_cast<float>(1ULL << 24);
}
}  // namespace

QVector3D GalaxyGlWidget::basePosition(int slotIndex, float outerRadius, bool departing) const {
    const quint64 seed = departing ? departingLayoutSeed_ : layoutSeed_;
    const int count = departing ? departingSlotCount_ : slotCount_;
    // Jittered Fermat spiral, pulled toward a few seeded cluster anchors.
    const float theta = static_cast<float>(slotIndex) * kGoldenAngle +
                        (layoutHash01(seed, slotIndex, 1) - 0.5f) * 0.9f;
    const float radius = kRadiusStep * std::sqrt(static_cast<float>(slotIndex) + 1.0f) *
                         (0.8f + 0.5f * layoutHash01(seed, slotIndex, 2));
    QVector3D pos(radius * std::cos(theta),
                  (layoutHash01(seed, slotIndex, 3) - 0.5f) * 2.0f * 7.0f,
                  radius * std::sin(theta));

    const int anchors = std::max(2, count / 10);
    QVector3D best;
    float bestD = 3.4e38f;
    for (int a = 0; a < anchors; ++a) {
        const float aAng = layoutHash01(seed, a, 100) * 6.2831853f;
        const float aRad = outerRadius * (0.25f + 0.55f * layoutHash01(seed, a, 101));
        const QVector3D anchor(aRad * std::cos(aAng), (layoutHash01(seed, a, 102) - 0.5f) * 6.0f,
                                aRad * std::sin(aAng));
        const float d = (anchor - pos).lengthSquared();
        if (d < bestD) {
            bestD = d;
            best = anchor;
        }
    }
    return pos + (best - pos) * 0.35f;
}

QVector3D GalaxyGlWidget::slotPosition(int slotIndex, bool departing) const {
    const quint64 seed = departing ? departingLayoutSeed_ : layoutSeed_;
    const int count = departing ? departingSlotCount_ : slotCount_;
    const float outer = kRadiusStep * std::sqrt(static_cast<float>(std::max(count, 1)));
    // ~1 in 9 slots is a companion of its predecessor's base position (no chains).
    if (slotIndex > 0 && layoutHash01(seed, slotIndex, 7) < 0.11f) {
        const QVector3D primary = basePosition(slotIndex - 1, outer, departing);
        const float ang = layoutHash01(seed, slotIndex, 8) * 6.2831853f;
        const float lift = (layoutHash01(seed, slotIndex, 9) - 0.5f) * 2.0f;
        return primary + QVector3D(std::cos(ang) * 2.4f, lift * 1.6f, std::sin(ang) * 2.4f);
    }
    return basePosition(slotIndex, outer, departing);
}

float GalaxyGlWidget::slotYaw(int slotIndex, bool departing) const {
    return layoutHash01(departing ? departingLayoutSeed_ : layoutSeed_, slotIndex, 5) * 360.0f;
}

float GalaxyGlWidget::slotPitch(int slotIndex, bool departing) const {
    return (layoutHash01(departing ? departingLayoutSeed_ : layoutSeed_, slotIndex, 6) - 0.5f) * 2.0f *
           35.0f;  // +-35 degree tilt
}

float GalaxyGlWidget::slotScale(int slotIndex, bool departing) const {
    // 0.7x .. 1.5x world radius.
    return 0.7f + 0.8f * layoutHash01(departing ? departingLayoutSeed_ : layoutSeed_, slotIndex, 4);
}

QVector3D GalaxyGlWidget::sectorCenter() const {
    return QVector3D(0.0f, 0.0f, 0.0f);
}

QVector3D GalaxyGlWidget::cameraEye() const {
    const float azRad = qDegreesToRadians(azimuth_);
    const float elRad = qDegreesToRadians(elevation_);
    const QVector3D offset(distance_ * std::cos(elRad) * std::sin(azRad), distance_ * std::sin(elRad),
                            distance_ * std::cos(elRad) * std::cos(azRad));
    return orbitTarget_ + offset;
}

void GalaxyGlWidget::mousePressEvent(QMouseEvent* event) {
    if (jumpVisual_ == JumpVisual::Jumping) {
        return;  // camera locks forward during jump
    }
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastMousePos_ = event->position().toPoint();
    }
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        lastMousePos_ = event->position().toPoint();
    }
}

void GalaxyGlWidget::mouseMoveEvent(QMouseEvent* event) {
    if (jumpVisual_ == JumpVisual::Jumping || (!dragging_ && !panning_)) {
        return;
    }
    const QPoint currentPos = event->position().toPoint();
    const QPoint delta = currentPos - lastMousePos_;
    lastMousePos_ = currentPos;

    if (panning_) {
        // Middle-drag pan in the view plane; moves both live and eased targets.
        const float azRad = qDegreesToRadians(azimuth_);
        const float elRad = qDegreesToRadians(elevation_);
        const QVector3D fwd(-std::cos(elRad) * std::sin(azRad), -std::sin(elRad),
                             -std::cos(elRad) * std::cos(azRad));
        const QVector3D right = QVector3D::crossProduct(fwd, QVector3D(0.0f, 1.0f, 0.0f)).normalized();
        const QVector3D camUp = QVector3D::crossProduct(right, fwd);
        const float worldPerPixel =
            2.0f * std::tan(qDegreesToRadians(22.5f)) * distance_ / static_cast<float>(std::max(height(), 1));
        const QVector3D pan = (right * -static_cast<float>(delta.x()) + camUp * static_cast<float>(delta.y())) *
                              worldPerPixel;
        orbitTarget_ += pan;
        targetOrbit_ += pan;
        update();
        return;
    }
    if (!dragging_) {
        return;
    }
    // Orbit sensitivity scales with the larger of dolly and nearest-galaxy distance.
    const float framing =
        (kRadiusStep * std::sqrt(static_cast<float>(std::max(slotCount_, 1))) + kGalaxyWorldRadius) * 2.2f;
    float nearestGal = 3.4e38f;
    const QVector3D eye = cameraEye();
    for (const GpuGalaxy& g : galaxies_) {
        nearestGal = std::min(nearestGal, (slotPosition(g.slotIndex) - eye).length());
    }
    const float sceneDist = nearestGal < 1.0e30f ? std::max(distance_, nearestGal) : distance_;
    const float sens = kOrbitSensitivity * std::clamp(sceneDist / std::max(framing, 1.0f), 0.02f, 1.0f);
    azimuth_ += static_cast<float>(delta.x()) * sens;
    elevation_ = std::clamp(elevation_ - static_cast<float>(delta.y()) * sens, -kElevationLimit,
                             kElevationLimit);
    update();
}

void GalaxyGlWidget::mouseReleaseEvent(QMouseEvent* event) {
    // Allowed mid-jump, or dragging_ would stick.
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
    }
    if (event->button() == Qt::MiddleButton) {
        panning_ = false;
    }
}

void GalaxyGlWidget::recenterTarget() {
    if (jumpVisual_ == JumpVisual::Jumping) {
        return;  // camera locks forward during jump
    }
    targetOrbit_ = sectorCenter();
    update();
}

void GalaxyGlWidget::resetCamera() {
    if (jumpVisual_ == JumpVisual::Jumping) {
        return;
    }
    // beginSector's framing pose; angles snap, dolly and target ease.
    const float outerRadius =
        kRadiusStep * std::sqrt(static_cast<float>(std::max(slotCount_, 1))) + kGalaxyWorldRadius;
    azimuth_ = 35.0f;
    elevation_ = 25.0f;
    targetOrbit_ = sectorCenter();
    targetDistance_ = std::clamp(outerRadius * 2.2f, kMinDistance, kMaxDistance);
    update();
}

void GalaxyGlWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        recenterTarget();
    }
}

void GalaxyGlWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        resetCamera();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void GalaxyGlWidget::wheelEvent(QWheelEvent* event) {
    if (jumpVisual_ == JumpVisual::Jumping) {
        return;  // camera locks forward during jump
    }
    const double notches = event->angleDelta().y();
    const double factor = std::pow(kZoomFactorPerNotch, notches);
    const float newDistance =
        std::clamp(static_cast<float>(targetDistance_ * factor), kMinDistance, kMaxDistance);
    const float applied = newDistance / targetDistance_;  // clamp-aware: drift must match the real dolly

    // Zoom toward the cursor. Math uses the eased end state so fast spins compound.
    const float azRad = qDegreesToRadians(azimuth_);
    const float elRad = qDegreesToRadians(elevation_);
    const QVector3D offset(targetDistance_ * std::cos(elRad) * std::sin(azRad),
                            targetDistance_ * std::sin(elRad),
                            targetDistance_ * std::cos(elRad) * std::cos(azRad));
    const QVector3D eye = targetOrbit_ + offset;
    const QVector3D fwd = (targetOrbit_ - eye).normalized();
    const QVector3D right = QVector3D::crossProduct(fwd, QVector3D(0.0f, 1.0f, 0.0f)).normalized();
    const QVector3D camUp = QVector3D::crossProduct(right, fwd);
    const float tanHalfFov = std::tan(qDegreesToRadians(22.5f));
    const float ndcX = 2.0f * static_cast<float>(event->position().x()) / static_cast<float>(width()) - 1.0f;
    const float ndcY = 1.0f - 2.0f * static_cast<float>(event->position().y()) / static_cast<float>(height());
    const QVector3D ray =
        (fwd + right * (ndcX * tanHalfFov * aspect_) + camUp * (ndcY * tanHalfFov)).normalized();
    const float along = std::max(QVector3D::dotProduct(ray, fwd), 0.05f);  // never divide by ~0
    // Anchor on the nearest galaxy the ray hits; the target plane is the fallback.
    float anchorT = targetDistance_ / along;
    float bestT = -1.0f;
    for (const GpuGalaxy& g : galaxies_) {
        const QVector3D c = slotPosition(g.slotIndex);
        const float tc = QVector3D::dotProduct(c - eye, ray);
        if (tc <= 0.0f) {
            continue;  // behind the camera
        }
        const float missSq = (eye + ray * tc - c).lengthSquared();
        const float r = kGalaxyWorldRadius * slotScale(g.slotIndex) * 1.3f;
        if (missSq <= r * r && (bestT < 0.0f || tc < bestT)) {
            bestT = tc;
        }
    }
    if (bestT > 0.0f) {
        anchorT = bestT;
    }
    const QVector3D anchor = eye + ray * anchorT;
    if (applied >= 0.999f && factor < 1.0) {
        // Dolly floored: zoom becomes flight along the cursor ray, with a stride
        // from the scene distance (min half a galaxy radius).
        float sceneT = anchorT;
        if (bestT <= 0.0f) {
            float nearestGal = 3.4e38f;
            for (const GpuGalaxy& g : galaxies_) {
                nearestGal = std::min(nearestGal, (slotPosition(g.slotIndex) - eye).length());
            }
            if (nearestGal < 1.0e30f) {
                sceneT = std::max(anchorT, nearestGal);
            }
        }
        const float stride =
            std::max(sceneT, kGalaxyWorldRadius * 0.5f) * (1.0f - static_cast<float>(factor));
        targetOrbit_ += ray * stride;
    } else {
        targetOrbit_ = anchor + (targetOrbit_ - anchor) * applied;
    }
    targetDistance_ = newDistance;
    update();
}
