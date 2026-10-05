// C ABI for a deterministic, pure-CPU part generator and AO baker.
// No Qt/ordo/OpenGL dependency; a plain shared library.
#pragma once

#include <stdint.h>

#if defined(_WIN32)
    #if defined(WORKLIB_BUILD)
        #define WORKLIB_API __declspec(dllexport)
    #else
        #define WORKLIB_API __declspec(dllimport)
    #endif
#else
    #define WORKLIB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// A baked part: closed, outward-wound triangle mesh plus one AO scalar per
// vertex (0 = fully shadowed, 1 = fully open sky). Pointers owned by worklib.
// A mid-bake snapshot may carry -1.0 aoValues entries; the final `out` never does.
typedef struct WorklibMesh {
    float*    positions;   /* xyz * vertexCount */
    float*    normals;     /* xyz * vertexCount */
    float*    aoValues;    /* vertexCount entries, 0..1 (1 = fully open) */
    uint32_t* indices;     /* 3 * triangleCount */
    uint32_t  vertexCount;
    uint32_t  triangleCount;
} WorklibMesh;

/* Called on the calling thread; `snapshot` is valid only during the call.
   The first call has fraction 0.0 (geometry done, every aoValues -1.0);
   there is no 1.0 call. */
typedef void (*WorklibProgressFn)(const WorklibMesh* snapshot, float fraction, void* user);

// Generates one part and bakes its ambient occlusion. Deterministic in
// (seed, smoothIters, aoRaysPerVertex), thread-safe, no locking.
// `out`'s buffers are allocated inside worklib -- free only via
// worklib_free_mesh. `progress` may be NULL and is never called on failure.
// Returns 0 on success, nonzero (zeroing *out) if `out` is NULL,
// aoRaysPerVertex is outside [1, 4096], or smoothIters exceeds 10000.
WORKLIB_API int worklib_bake_part(uint64_t seed, uint32_t smoothIters,
                                   uint32_t aoRaysPerVertex,
                                   WorklibProgressFn progress, void* user,
                                   WorklibMesh* out);

// Frees a mesh produced by worklib_bake_part and zeroes the struct. Safe to
// call on an already-freed (zeroed) mesh, and safe to call with mesh == NULL.
WORKLIB_API void worklib_free_mesh(WorklibMesh* mesh);

#ifdef __cplusplus
}
#endif
