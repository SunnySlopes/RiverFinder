//
// Created by zhdds on 2026/3/15.
//

#ifndef RIVERFINDER_RIVERFINDERLIB_H
#define RIVERFINDER_RIVERFINDERLIB_H

#ifdef RIVERFINDER_EXPORTS
#define RIVERFINDER_API __declspec(dllexport)
#else
#define RIVERFINDER_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Single-phase river search.
 * @return [x1, z1, river1, cave1, ...] terminated by 0,0,0,0 or null.
 *
 * Legacy entry point. It keeps the old behaviour and uses MC_1_21_3 as the
 * generation base. New callers should use riverSearchEx() and pass MC_26_1
 * or MC_26_3 explicitly.
 */
RIVERFINDER_API int *riverSearch(long long seed, int startX, int startZ,
                                 int width, int height, int y, int minArea,
                                 int numThreads, float caveFactor);

/**
 * River search with an explicit Minecraft version.
 *
 * @param mc cubiomes MC version id (e.g. MC_26_1, MC_26_3).
 * @return [x1, z1, riverArea1, caveArea1, ...] terminated by 0,0,0,0 or null.
 */
RIVERFINDER_API int *riverSearchEx(long long seed, int mc,
                                   int startX, int startZ,
                                   int width, int height, int y, int minArea,
                                   int numThreads, float caveFactor);

RIVERFINDER_API int *getSearchProgress();

#ifdef __cplusplus
}
#endif

#endif //RIVERFINDER_RIVERFINDERLIB_H
