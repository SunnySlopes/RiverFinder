/* cubiomes' rng.h defines a C helper named lerp(), while C++20 <cmath>
 * injects std::lerp into the global namespace. Rename the cubiomes helper
 * while rng.h is first included, then release the macro before the other
 * headers pull in <math.h>. */
#define lerp cubiomes_lerp
#include <cubiomes/rng.h>
#undef lerp
#include <cubiomes/generator.h>
#include <cubiomes/biomes.h>
#include <cubiomes/biomenoise.h>
#include "Thread.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <compare>
#include <functional>
#include <iostream>
#include <queue>
#include <ranges>
#include <unordered_map>
#include <vector>
#define AREA_SIZE 256

namespace riverfinder {

/* ================== 河流 / 溶洞判定 ================== */

/* 旧版 cubiomes 里的 river-only 快速预筛。
 *
 * 它只负责在粗筛阶段尽早排除明显不是河流的格子；真正是否河流仍由
 * climateToBiome() 给出。这里改用外部（主仓）cubiomes 的公开 API：
 * sampleDoublePerlin / getSpline / climateToBiome，不再依赖旧版
 * cubiomes 里那个同名但未导出的 sampleBiomeNoiseOnRiver()。
 *
 * 阈值来自旧实现，语义是“保守预筛”：宁可多放行，也不要把可能的河流
 * 提前排除。26.2+ 的 ridge 折叠只改变 weirdness 的分布，不改变下面这些
 * 气候分量的含义；如果后续验证发现漏检，可把 RIVER_STRICT_BIOME 打开，
 * 直接用完整的 sampleBiomeNoise()。
 */
static inline int sampleRiverBiomeNoise(const BiomeNoise *bn, int x, int y, int z,
                                        uint32_t sample_flags)
{
#ifdef RIVER_STRICT_BIOME
    (void)bn;
    return 0; /* unused; the call site uses sampleBiomeNoise() instead */
#else
    if (bn->nptype >= 0)
    {
        int64_t id = (int64_t) (10000.0 * sampleClimatePara(bn, nullptr, x, z));
        return (int) id;
    }

    float t = 0, h = 0, c = 0, e = 0, d = 0, w = 0;
    double px = x, pz = z;
    if (!(sample_flags & SAMPLE_NO_SHIFT))
    {
        px += sampleDoublePerlin(&bn->climate[NP_SHIFT], x, 0, z) * 4.0;
        pz += sampleDoublePerlin(&bn->climate[NP_SHIFT], z, x, 0) * 4.0;
    }

    w = sampleDoublePerlin(&bn->climate[NP_WEIRDNESS], px, 0, pz);
    if (w > 0.05 || w < -0.05) return 0;
    c = sampleDoublePerlin(&bn->climate[NP_CONTINENTALNESS], px, 0, pz);
    if (c < -0.19) return 0;
    e = sampleDoublePerlin(&bn->climate[NP_EROSION], px, 0, pz);
    if (c > 0.03f && e < -0.375f) return 0;
    t = sampleDoublePerlin(&bn->climate[NP_TEMPERATURE], px, 0, pz);
    if (t < -0.45) return 0;
    if (e > 0.55 && c >= -0.11) return 0;

    if (!(sample_flags & SAMPLE_NO_DEPTH))
    {
        float np_param[] = {
            c, e, -3.0F * (fabsf(fabsf(w) - 0.6666667F) - 0.33333334F), w,
        };
        double off = getSpline(bn->sp, np_param) + 0.015F;
        d = 1.0 - (y * 4) / 128.0 - 83.0 / 160.0 + off;
    }

    h = sampleDoublePerlin(&bn->climate[NP_HUMIDITY], px, 0, pz);

    int64_t l_np[6];
    l_np[0] = (int64_t) (10000.0F * t);
    l_np[1] = (int64_t) (10000.0F * h);
    l_np[2] = (int64_t) (10000.0F * c);
    l_np[3] = (int64_t) (10000.0F * e);
    l_np[4] = (int64_t) (10000.0F * d);
    l_np[5] = (int64_t) (10000.0F * w);

    if (sample_flags & SAMPLE_NO_BIOME)
        return none;
    return climateToBiome(bn->mc, (const uint64_t *) l_np, nullptr);
#endif
}

static inline int riverBiomeIdAt(const BiomeNoise *bn, int x, int y, int z)
{
#ifdef RIVER_STRICT_BIOME
    return sampleBiomeNoise(bn, nullptr, x, y, z, nullptr, SAMPLE_NO_SHIFT);
#else
    return sampleRiverBiomeNoise(bn, x, y, z, SAMPLE_NO_SHIFT);
#endif
}

static inline int isRiverBiomeId(int id)
{
    return id == river;
}

static inline int isCaveBiomeId(int id)
{
    return id == dripstone_caves;
}

static inline int scoreFromAreas(int riverArea, int caveArea, double caveFactor)
{
    return riverArea + static_cast<int>(caveArea * caveFactor + (caveFactor >= 0 ? 0.5 : -0.5));
}

struct Point {
    int x = 0;
    int y = 0;
    std::strong_ordering operator<=>(const Point &) const = default;
};

struct Res {
    Point point;
    int riverArea = 0;
    int caveArea = 0;
    /** river + cave * caveFactor */
    int score = 0;

    bool operator<(const Res &other) const noexcept
    {
        if (score != other.score)
            return score < other.score;
        if (riverArea != other.riverArea)
            return riverArea < other.riverArea;
        if (caveArea != other.caveArea)
            return caveArea < other.caveArea;
        return point < other.point;
    }

    bool operator>(const Res &other) const noexcept
    {
        return other < *this;
    }

    Res() = default;

    Res(Point point, int riverArea, int caveArea, int score)
        : point(point), riverArea(riverArea), caveArea(caveArea), score(score)
    {
    }
};

struct Progress {
    std::atomic_int current = 0;
    std::atomic_int total = 0;
    std::atomic_int chunkInRunning{0};
    std::atomic_int phase1 = 0;
    std::atomic_bool try_pause = false;
    std::atomic_bool try_stop = false;
};

template<int scale>
std::vector<Res> findBiggestRiver(
    Generator *g,
    int startX, int startZ,
    int sx, int sz,
    int y,
    int minScore,
    double preserveRange = 0.99,
    double caveFactor = 0.0) noexcept
{
    std::vector<Res> result;

    const int W = sx / scale;
    const int H = sz / scale;
    if (W <= 0 || H <= 0)
        return result;

    const int stride = W + 1;
    const bool countCave = caveFactor != 0.0;

    std::vector<int> rawRiver(W * H);
    std::vector<int> rawCave(countCave ? W * H : 0);
    std::vector<int> prefixRiver((W + 1) * (H + 1), 0);
    std::vector<int> prefixCave(countCave ? (W + 1) * (H + 1) : 0, 0);

#define RAW_R(x, z) rawRiver[(x) + (z) * W]
#define RAW_C(x, z) rawCave[(x) + (z) * W]
#define ARR_R(x, z) prefixRiver[(x) + (z) * stride]
#define ARR_C(x, z) prefixCave[(x) + (z) * stride]

    if constexpr (scale > 1)
    {
        for (int z = 0; z < H; z++)
        {
            int worldZ = startZ + z * scale + scale / 2;
            for (int x = 0; x < W; x++)
            {
                int worldX = startX + x * scale + scale / 2;
                int id = riverBiomeIdAt(
                    &g->bn,
                    worldX / 4,
                    y / 4 + 1,
                    worldZ / 4);
                int river = isRiverBiomeId(id);
                RAW_R(x, z) = river;
                if (countCave)
                    RAW_C(x, z) = river ? 0 : isCaveBiomeId(id);
            }
        }
    }
    else
    {
        for (int z = 0; z < H; z++)
        {
            int worldZ = startZ + z;
            for (int x = 0; x < W; x++)
            {
                int worldX = startX + x;
                int id = getBiomeAt(g, 1, worldX, y, worldZ);
                int river = isRiverBiomeId(id);
                RAW_R(x, z) = river;
                if (countCave)
                    RAW_C(x, z) = river ? 0 : isCaveBiomeId(id);
            }
        }
    }

    for (int z = 1; z <= H; z++)
    {
        for (int x = 1; x <= W; x++)
        {
            ARR_R(x, z) =
                    RAW_R(x - 1, z - 1)
                    + ARR_R(x - 1, z)
                    + ARR_R(x, z - 1)
                    - ARR_R(x - 1, z - 1);
            if (countCave)
            {
                ARR_C(x, z) =
                        RAW_C(x - 1, z - 1)
                        + ARR_C(x - 1, z)
                        + ARR_C(x, z - 1)
                        - ARR_C(x - 1, z - 1);
            }
        }
    }

    struct RiverArea {
        int score;
        int riverArea;
        int caveArea;
        int startX;
        int startZ;

        bool operator<(const RiverArea &other) const noexcept
        {
            if (score != other.score)
                return score < other.score;
            if (startX != other.startX)
                return startX > other.startX;
            return startZ > other.startZ;
        }
    };

    std::priority_queue<RiverArea> pq;

    const int R_out = 128 / scale;
    const int R_in = 24 / scale;

    std::vector<int> dxOut(2 * R_out + 1);
    std::vector<int> dxIn(2 * R_in + 1);

    for (int dz = -R_out; dz <= R_out; dz++)
        dxOut[dz + R_out] = (int) floor(sqrt((double) R_out * R_out - dz * dz));

    for (int dz = -R_in; dz <= R_in; dz++)
        dxIn[dz + R_in] = (int) floor(sqrt((double) R_in * R_in - dz * dz));

    struct Mask {
        int dz;
        int out;
        int in;
    };

    std::vector<Mask> mask;
    mask.reserve(2 * R_out + 1);

    for (int dz = -R_out; dz <= R_out; dz++)
    {
        Mask m{};
        m.dz = dz;
        m.out = dxOut[dz + R_out];
        m.in = (abs(dz) <= R_in) ? dxIn[dz + R_in] : -1;
        mask.push_back(m);
    }

    auto ringSum = [&](const std::vector<int> &prefix, int cx, int cz) -> int {
        int area = 0;
#define ARR(x, z) prefix[(x) + (z) * stride]
        for (const Mask &m: mask)
        {
            int row = cz + m.dz;
            int L = cx - m.out;
            int R = cx + m.out;
            if (m.in == -1)
            {
                area += ARR(R + 1, row + 1) - ARR(L, row + 1) - ARR(R + 1, row) + ARR(L, row);
            }
            else
            {
                int Lin = cx - m.in;
                int Rin = cx + m.in;
                area += ARR(Lin, row + 1) - ARR(L, row + 1) - ARR(Lin, row) + ARR(L, row);
                area += ARR(R + 1, row + 1) - ARR(Rin + 1, row + 1) - ARR(R + 1, row) + ARR(Rin + 1, row);
            }
        }
#undef ARR
        return area;
    };

    RiverArea maxArea = {0, 0, 0, 0, 0};

    for (int cz = R_out; cz < H - R_out; cz++)
    {
        for (int cx = R_out; cx < W - R_out; cx++)
        {
            int riverCells = ringSum(prefixRiver, cx, cz);
            int caveCells = countCave ? ringSum(prefixCave, cx, cz) : 0;
            int worldRiver = riverCells * scale * scale;
            int worldCave = caveCells * scale * scale;
            int worldScore = scoreFromAreas(worldRiver, worldCave, caveFactor);

            if (worldScore >= maxArea.score * preserveRange && worldScore >= minScore)
            {
                int worldX = startX + cx * scale;
                int worldZ = startZ + cz * scale;
                pq.push({worldScore, worldRiver, worldCave, worldX, worldZ});
                if (worldScore > maxArea.score)
                    maxArea = {worldScore, worldRiver, worldCave, worldX, worldZ};
            }
        }
    }

    while (!pq.empty())
    {
        const auto &ra = pq.top();
        if (ra.score >= maxArea.score * preserveRange)
        {
            result.emplace_back(Point{ra.startX, ra.startZ}, ra.riverArea, ra.caveArea, ra.score);
            if (preserveRange == 1.0)
                break;
        }
        pq.pop();
    }

#undef RAW_R
#undef RAW_C
#undef ARR_R
#undef ARR_C
    return result;
}

void findBiggestRiverParallelPool(
    ThreadSafeResults<Res> &globalResults,
    Generator *g,
    int startX,
    int startZ,
    int sx,
    int sz,
    int y,
    int minScore,
    double caveFactor,
    Progress *progress,
    int numThreads = 0)
{
    if (sx <= 0 || sz <= 0)
        return;
    if (numThreads <= 0)
        numThreads = static_cast<int>(std::thread::hardware_concurrency());
    if (numThreads <= 0)
        numThreads = 1;

    const int chunkSize = 4096 * 2;
    const int overlap = 256;
    const int step = chunkSize - overlap;
    const int nx = (sx + step - 1) / step;
    const int nz = (sz + step - 1) / step;

    /* Count only chunks that are large enough for the scale-16 pass. This is
     * also the exact progress denominator; it is computed before any worker
     * starts, so the clamped edge chunks cannot make the bar run backwards. */
    int totalChunks = 0;
    for (int ix = 0; ix < nx; ix++)
    {
        const int x = ix * step;
        const int currentSx = std::min(chunkSize, sx - x);
        if (currentSx < 256)
            continue;
        for (int iz = 0; iz < nz; iz++)
        {
            const int z = iz * step;
            const int currentSz = std::min(chunkSize, sz - z);
            if (currentSz >= 256)
                totalChunks++;
        }
    }

    if (progress)
    {
        progress->total = totalChunks;
        progress->current = 0;
        progress->chunkInRunning = 0;
    }
    if (totalChunks == 0)
        return;

    /* Streaming scheduler: workers pull the next chunk index from a single
     * atomic counter. There is no task queue, so a full-world search (tens of
     * millions of chunks) uses O(threads) memory instead of O(chunks). */
    const int slots = nx * nz;
    std::atomic<int> nextChunk{0};
    std::atomic<int> completedChunks{0};
    std::atomic<bool> failed{false};

    auto worker = [&]() {
        for (;;)
        {
            if (progress && progress->try_stop)
                return;

            if (progress)
            {
                while (progress->try_pause && !progress->try_stop)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (progress->try_stop)
                    return;
            }

            const int idx = nextChunk.fetch_add(1);
            if (idx >= slots)
                return;

            const int ix = idx % nx;
            const int iz = idx / nx;
            const int x = ix * step;
            const int z = iz * step;
            const int currentSx = std::min(chunkSize, sx - x);
            const int currentSz = std::min(chunkSize, sz - z);
            if (currentSx < 256 || currentSz < 256)
                continue;

            if (progress)
                progress->chunkInRunning += 1;

            try
            {
                Generator localG = *g;
                auto blockResultsX16 = findBiggestRiver<16>(
                    &localG,
                    startX + x,
                    startZ + z,
                    currentSx,
                    currentSz,
                    y,
                    minScore,
                    0.85,
                    caveFactor);

                int bx = currentSx / 256 + 2;
                int bz = currentSz / 256 + 2;
                std::vector<Res> flags(bx * bz);
                for (const auto &it: blockResultsX16)
                {
                    int x2 = (it.point.x - startX - x) / 256;
                    int z2 = (it.point.y - startZ - z) / 256;
                    if (x2 < 0 || z2 < 0 || x2 >= bx || z2 >= bz)
                        continue;
                    auto &slot = flags[x2 + bx * z2];
                    if (slot.score < it.score)
                        slot = it;
                }

                std::vector<Res> pqX16;
                pqX16.reserve(flags.size());
                for (auto &kv: flags)
                    if (kv.score > 0)
                        pqX16.push_back(kv);
                std::ranges::sort(pqX16, [](const Res &a, const Res &b) { return a.score > b.score; });

                std::unordered_map<uint64_t, Res> blockResultsX4;
                blockResultsX4.reserve(16);
                int maxScore = 0;

                for (auto &res: pqX16)
                {
                    if (progress && progress->try_stop)
                        break;
                    while (progress && progress->try_pause)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        if (progress->try_stop)
                            break;
                    }

                    auto subResults = findBiggestRiver<4>(
                        &localG,
                        res.point.x - 256,
                        res.point.y - 256,
                        512,
                        512,
                        y,
                        minScore,
                        1.0,
                        caveFactor);

                    if (subResults.empty())
                        break;
                    if (subResults[0].score < maxScore * 0.9)
                        break;

                    auto &r = subResults[0];
                    uint64_t key = (uint64_t(uint32_t(r.point.x)) << 32) | uint32_t(r.point.y);
                    auto it = blockResultsX4.find(key);
                    if (it == blockResultsX4.end())
                        blockResultsX4.emplace(key, r);
                    else if (it->second.score < r.score)
                        it->second = r;

                    if (r.score > maxScore)
                        maxScore = r.score;
                }

                std::vector<Res> filteredResults;
                filteredResults.reserve(blockResultsX4.size());
                for (const auto &result: blockResultsX4 | std::views::values)
                {
                    int relX = result.point.x - (startX + x);
                    int relZ = result.point.y - (startZ + z);
                    if (relX > overlap / 2 && relX < currentSx - overlap / 2 &&
                        relZ > overlap / 2 && relZ < currentSz - overlap / 2 &&
                        result.score > 0)
                    {
                        filteredResults.push_back(result);
                    }
                }
                std::ranges::sort(filteredResults, [](const Res &a, const Res &b) { return a.score > b.score; });
                if (!filteredResults.empty())
                    globalResults.addResults(filteredResults);
            }
            catch (...)
            {
                /* A worker failure must not take down the whole process. Keep
                 * the chunks that already completed and let the caller see the
                 * progress stall rather than std::terminate. */
                failed.store(true);
            }

            if (progress)
            {
                progress->chunkInRunning -= 1;
                progress->current = completedChunks.fetch_add(1) + 1;
            }
            else
            {
                completedChunks.fetch_add(1);
            }
        }
    };

    const int workerCount = std::min(numThreads, totalChunks);
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(workerCount));
    for (int i = 0; i < workerCount; i++)
        workers.emplace_back(worker);
    for (auto &thread: workers)
        thread.join();

    if (progress)
    {
        progress->chunkInRunning = 0;
        if (failed.load())
            progress->try_stop = true;
    }
}


} // namespace riverfinder
