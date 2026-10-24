//
// Created by zhdds on 2026/3/15.
//

#ifndef RIVERFINDER_EXPORTS
#define RIVERFINDER_EXPORTS
#endif
#include "riverFinderLib.h"

#include <cubiomes/biomes.h>

#include <cstring>
#include <cstdlib>
#include <ranges>

#include "river_finder.cpp"

using namespace riverfinder;

static int *riverSearchImpl(long long seed, int mc,
                            int startX, int startZ, int width, int height, int y, int minArea,
                            int numThreads, float caveFactor)
{
    if (mc <= MC_UNDEF || mc < MC_1_18 || mc > MC_NEWEST)
        mc = MC_26_1;

    Progress progress{};
    Generator g;
    setupGenerator(&g, mc, FORCE_OCEAN_VARIANTS);
    applySeed(&g, DIM_OVERWORLD, seed);

    const double caveF = static_cast<double>(caveFactor);
    ThreadSafeResults<Res> globalResults;
    progress.phase1 = 1;
    if (numThreads > 0)
        findBiggestRiverParallelPool(globalResults, &g, startX, startZ, width, height, y, minArea, caveF, &progress,
                                     numThreads);
    else
        findBiggestRiverParallelPool(globalResults, &g, startX, startZ, width, height, y, minArea, caveF, &progress);

    auto res = globalResults.getAllResults();
    int maxScore = 0;
    std::vector<Res> finallyResults;
    for (const auto &it: res)
    {
        auto temp = findBiggestRiver<1>(&g, it.point.x - 128 - 32, it.point.y - 128 - 32, 256 + 64, 256 + 64, y, 1, 1.0,
                                        caveF);
        if (!temp.empty() && temp[0].score > maxScore * 0.9)
        {
            finallyResults.push_back(temp[0]);
            if (temp[0].score > maxScore)
                maxScore = temp[0].score;
        }
        else
        {
            break;
        }
    }
    std::ranges::sort(finallyResults, [](const Res &a, const Res &b) { return a.score > b.score; });
    int *result = (int *) malloc(finallyResults.size() * 4 * sizeof(int) + 4 * sizeof(int));
    int i = 0;
    for (auto &it: finallyResults)
    {
        result[i++] = it.point.x;
        result[i++] = it.point.y;
        result[i++] = it.riverArea;
        result[i++] = it.caveArea;
    }
    result[i++] = 0;
    result[i++] = 0;
    result[i++] = 0;
    result[i++] = 0;
    return result;
}

int *riverSearch(long long seed, int startX, int startZ, int width, int height, int y, int minArea,
                 int numThreads, float caveFactor)
{
    /* Legacy behaviour: the old library was pinned to 1.21.3. */
    return riverSearchImpl(seed, MC_1_21_3, startX, startZ, width, height, y, minArea,
                           numThreads, caveFactor);
}

int *riverSearchEx(long long seed, int mc, int startX, int startZ, int width, int height, int y, int minArea,
                   int numThreads, float caveFactor)
{
    return riverSearchImpl(seed, mc, startX, startZ, width, height, y, minArea,
                           numThreads, caveFactor);
}
