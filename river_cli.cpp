/*
 * Standalone RiverFinder command-line program.
 *
 * This translation unit uses only the public C ABI from riverFinderLib.h. The
 * static riverfinder library already contains the cubiomes sources, so the
 * resulting executable is self-contained.
 */

#include "riverFinderLib.h"

#include <cubiomes/biomes.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char *kCliVersion = "1.0.0";

/* Same geometry as the Java GUI: the result is a ring with inner radius 24
 * and outer radius 128. The ratio is only a presentation value. */
constexpr int kRingOuter = 128;
constexpr double kFullRingArea = 49640.0;

/* The GUI keeps one maximum per 64x64 block cell. */
constexpr int kOutputCell = 64;

constexpr int kDefaultMinX = -65536;
constexpr int kDefaultMaxX = 65535;
constexpr int kDefaultMinZ = -65536;
constexpr int kDefaultMaxZ = 65535;
constexpr int kMaxSide = 60000001;
constexpr int kDefaultMinArea = 34763;
constexpr int kDefaultY = -64;
constexpr int kDefaultLimit = 1000;
constexpr long long kMaxSearchArea = 30000000LL * 30000000LL;

struct Options {
    long long seed = 0;
    bool seedSet = false;

    int mc = MC_26_3;

    int minX = kDefaultMinX;
    int maxX = kDefaultMaxX;
    int minZ = kDefaultMinZ;
    int maxZ = kDefaultMaxZ;
    int square = 0;
    bool boundsSet = false;

    int minArea = kDefaultMinArea;
    int y = kDefaultY;
    int threads = 0;
    double caveFactor = 1.0;

    int limit = kDefaultLimit;
    std::string outPath;

    bool help = false;
    bool version = false;
};

struct Hit {
    int x = 0;
    int z = 0;
    int river = 0;
    int cave = 0;
    int score = 0;
};

struct VersionAlias {
    const char *name;
    int mc;
};

constexpr VersionAlias kVersionAliases[] = {
    {"1.18", MC_1_18},
    {"1.18.1", MC_1_18},
    {"1.18.2", MC_1_18},
    {"1.19", MC_1_19_2},
    {"1.19.2", MC_1_19_2},
    {"1.19.4", MC_1_19_4},
    {"1.20", MC_1_20_6},
    {"1.20.6", MC_1_20_6},
    {"1.21", MC_1_21_11},
    {"1.21.1", MC_1_21_1},
    {"1.21.3", MC_1_21_3},
    {"1.21.4", MC_1_21_4},
    {"1.21.5", MC_1_21_5},
    {"1.21.6", MC_1_21_6},
    {"1.21.9", MC_1_21_9},
    {"1.21.11", MC_1_21_11},
    {"26.1", MC_26_1},
    {"26.2", MC_26_2},
    {"26.3", MC_26_3},
    {"latest", MC_NEWEST},
};

std::string_view mcDisplayName(int mc)
{
    switch (mc)
    {
    case MC_1_18: return "1.18.2";
    case MC_1_19_2: return "1.19.2";
    case MC_1_19_4: return "1.19.4";
    case MC_1_20_6: return "1.20.6";
    case MC_1_21_1: return "1.21.1";
    case MC_1_21_3: return "1.21.3";
    case MC_1_21_4: return "1.21.4";
    case MC_1_21_5: return "1.21.5";
    case MC_1_21_6: return "1.21.6";
    case MC_1_21_9: return "1.21.9";
    case MC_1_21_11: return "1.21.11";
    case MC_26_1: return "26.1";
    case MC_26_2: return "26.2";
    case MC_26_3: return "26.3";
    default: return "unknown";
    }
}

void printUsage(std::ostream &os)
{
    os <<
        "RiverFinder standalone CLI\n"
        "\n"
        "Usage:\n"
        "  riverfinder-cli --seed <seed> [options]\n"
        "\n"
        "Required:\n"
        "  --seed <int64>          Minecraft world seed (decimal, may be negative)\n"
        "\n"
        "Search area (default: a 131072 x 131072 square around 0,0):\n"
        "  --min-x <int>           default -65536\n"
        "  --max-x <int>           default 65535\n"
        "  --min-z <int>           default -65536\n"
        "  --max-z <int>           default 65535\n"
        "  --square <side>         symmetric area centered on 0; cannot be combined\n"
        "                          with explicit min/max bounds\n"
        "\n"
        "Search parameters:\n"
        "  --mc <id|version>       generation base, default 26.3\n"
        "  --min-area <int>        minimum river area, default 34763\n"
        "  --y <int>               biome sample Y, default -64\n"
        "  --threads <int>         worker threads, default min(32, CPU count)\n"
        "  --cave-factor <float>   cave area weight, default 1.0\n"
        "  --no-cave               same as --cave-factor 0\n"
        "\n"
        "Output:\n"
        "  --limit <int>           maximum hits, default 1000; 0 means no extra\n"
        "                          truncation (the engine keeps about 1000 candidates)\n"
        "  --out <file>            also write the text report to <file>\n"
        "  --help, -h              show this help\n"
        "  --version               show CLI version\n"
        "\n"
        "Version names:\n"
        "  1.18, 1.18.1, 1.18.2, 1.19, 1.19.2, 1.19.4,\n"
        "  1.20, 1.20.6, 1.21, 1.21.1, 1.21.3, 1.21.4, 1.21.5, 1.21.6,\n"
        "  1.21.9, 1.21.11, 26.1, 26.2, 26.3, latest\n"
        "  A raw cubiomes MCVersion id between MC_1_18 and MC_NEWEST is also\n"
        "  accepted with --mc.\n";
}

[[noreturn]] void usageError(const std::string &message)
{
    std::cerr << "error: " << message << "\n\n";
    printUsage(std::cerr);
    std::exit(2);
}

[[noreturn]] void runtimeError(const std::string &message)
{
    std::cerr << "error: " << message << "\n";
    std::exit(1);
}

bool parseLongLong(std::string_view text, long long &value)
{
    if (text.empty())
        return false;
    const char *first = text.data();
    const char *last = text.data() + text.size();
    if (*first == '+')
        ++first;
    if (first == last)
        return false;
    auto result = std::from_chars(first, last, value, 10);
    return result.ec == std::errc() && result.ptr == last;
}

bool parseInt(std::string_view text, int &value)
{
    long long parsed = 0;
    if (!parseLongLong(text, parsed))
        return false;
    if (parsed < INT_MIN || parsed > INT_MAX)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

bool parseDouble(std::string_view text, double &value)
{
    if (text.empty())
        return false;
    std::string copy(text);
    errno = 0;
    char *end = nullptr;
    const double parsed = std::strtod(copy.c_str(), &end);
    if (errno != 0 || end == copy.c_str() || *end != '\0' || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

bool parseMc(std::string_view text, int &mc)
{
    int numeric = 0;
    if (parseInt(text, numeric))
    {
        if (numeric < MC_1_18 || numeric > MC_NEWEST)
            return false;
        mc = numeric;
        return true;
    }
    for (const VersionAlias &alias: kVersionAliases)
    {
        if (text == alias.name)
        {
            mc = alias.mc;
            return true;
        }
    }
    return false;
}

std::string_view requireValue(int argc, char **argv, int &index, std::string_view option)
{
    if (index + 1 >= argc)
        usageError("missing value for " + std::string(option));
    ++index;
    return argv[index];
}

Options parseOptions(int argc, char **argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            options.help = true;
        }
        else if (arg == "--version")
        {
            options.version = true;
        }
        else if (arg == "--seed")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseLongLong(value, options.seed))
                usageError("invalid --seed: " + std::string(value));
            options.seedSet = true;
        }
        else if (arg == "--mc")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseMc(value, options.mc))
                usageError("invalid --mc: " + std::string(value));
        }
        else if (arg == "--min-x")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.minX))
                usageError("invalid --min-x: " + std::string(value));
            options.boundsSet = true;
        }
        else if (arg == "--max-x")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.maxX))
                usageError("invalid --max-x: " + std::string(value));
            options.boundsSet = true;
        }
        else if (arg == "--min-z")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.minZ))
                usageError("invalid --min-z: " + std::string(value));
            options.boundsSet = true;
        }
        else if (arg == "--max-z")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.maxZ))
                usageError("invalid --max-z: " + std::string(value));
            options.boundsSet = true;
        }
        else if (arg == "--square")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.square))
                usageError("invalid --square: " + std::string(value));
            if (options.square <= 0 || options.square > kMaxSide)
                usageError("--square must be between 1 and " + std::to_string(kMaxSide));
        }
        else if (arg == "--min-area")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.minArea))
                usageError("invalid --min-area: " + std::string(value));
            if (options.minArea < 0)
                usageError("--min-area must not be negative");
        }
        else if (arg == "--y")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.y))
                usageError("invalid --y: " + std::string(value));
        }
        else if (arg == "--threads")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.threads))
                usageError("invalid --threads: " + std::string(value));
            if (options.threads < 1)
                usageError("--threads must be at least 1");
            if (options.threads > 256)
                usageError("--threads must not exceed 256");
        }
        else if (arg == "--cave-factor")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseDouble(value, options.caveFactor))
                usageError("invalid --cave-factor: " + std::string(value));
            if (options.caveFactor < 0.0)
                usageError("--cave-factor must not be negative");
        }
        else if (arg == "--no-cave")
        {
            options.caveFactor = 0.0;
        }
        else if (arg == "--limit")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.limit))
                usageError("invalid --limit: " + std::string(value));
            if (options.limit < 0)
                usageError("--limit must not be negative");
        }
        else if (arg == "--out")
        {
            options.outPath = std::string(requireValue(argc, argv, i, arg));
            if (options.outPath.empty())
                usageError("--out requires a non-empty path");
        }
        else
        {
            usageError("unknown option: " + std::string(arg));
        }
    }
    return options;
}

int combinedScore(int riverArea, int caveArea, double caveFactor)
{
    return riverArea + static_cast<int>(caveArea * caveFactor + (caveFactor >= 0.0 ? 0.5 : -0.5));
}

int floorDiv(int value, int divisor)
{
    int quotient = value / divisor;
    int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0)))
        --quotient;
    return quotient;
}

uint64_t packXZ(int x, int z)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32)
        | static_cast<uint32_t>(z);
}

int64_t originDistanceSquared(const Hit &hit)
{
    return static_cast<int64_t>(hit.x) * hit.x + static_cast<int64_t>(hit.z) * hit.z;
}

bool betterHit(const Hit &candidate, const Hit &current)
{
    if (candidate.score != current.score)
        return candidate.score > current.score;
    if (candidate.river != current.river)
        return candidate.river > current.river;
    if (candidate.cave != current.cave)
        return candidate.cave > current.cave;
    const int64_t candidateDistance = originDistanceSquared(candidate);
    const int64_t currentDistance = originDistanceSquared(current);
    if (candidateDistance != currentDistance)
        return candidateDistance < currentDistance;
    if (candidate.x != current.x)
        return candidate.x < current.x;
    return candidate.z < current.z;
}

std::vector<Hit> filterHits(const int *raw, size_t count, double caveFactor, int limit)
{
    std::unordered_map<uint64_t, Hit> byXZ;
    byXZ.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const int *quad = raw + i * 4;
        Hit hit;
        hit.x = quad[0];
        hit.z = quad[1];
        hit.river = quad[2];
        hit.cave = quad[3];
        hit.score = combinedScore(hit.river, hit.cave, caveFactor);

        const uint64_t key = packXZ(hit.x, hit.z);
        auto found = byXZ.find(key);
        if (found == byXZ.end())
            byXZ.emplace(key, hit);
        else if (betterHit(hit, found->second))
            found->second = hit;
    }

    std::unordered_map<uint64_t, Hit> byCell;
    byCell.reserve(byXZ.size());
    for (const auto &entry: byXZ)
    {
        const Hit &hit = entry.second;
        const uint64_t key = packXZ(floorDiv(hit.x, kOutputCell), floorDiv(hit.z, kOutputCell));
        auto found = byCell.find(key);
        if (found == byCell.end())
            byCell.emplace(key, hit);
        else if (betterHit(hit, found->second))
            found->second = hit;
    }

    std::vector<Hit> filtered;
    filtered.reserve(byCell.size());
    for (const auto &entry: byCell)
        filtered.push_back(entry.second);

    std::ranges::sort(filtered, [](const Hit &left, const Hit &right) {
        if (left.score != right.score)
            return left.score > right.score;
        const int64_t leftDistance = static_cast<int64_t>(left.x) * left.x
            + static_cast<int64_t>(left.z) * left.z;
        const int64_t rightDistance = static_cast<int64_t>(right.x) * right.x
            + static_cast<int64_t>(right.z) * right.z;
        if (leftDistance != rightDistance)
            return leftDistance < rightDistance;
        if (left.x != right.x)
            return left.x < right.x;
        return left.z < right.z;
    });

    if (limit > 0 && filtered.size() > static_cast<size_t>(limit))
        filtered.resize(static_cast<size_t>(limit));
    return filtered;
}

std::string formatReport(const Options &options, int startX, int startZ, int width, int height,
                         const std::vector<Hit> &hits)
{
    std::ostringstream out;
    out << "# RiverFinder CLI " << kCliVersion << "\n";
    out << "# seed=" << options.seed
        << " mc=" << mcDisplayName(options.mc) << "(" << options.mc << ")"
        << " search=[" << options.minX << "," << options.maxX << "]x["
        << options.minZ << "," << options.maxZ << "]"
        << " start=" << startX << "," << startZ
        << " size=" << width << "x" << height
        << " y=" << options.y
        << " minArea=" << options.minArea
        << " threads=" << options.threads
        << " caveFactor=" << options.caveFactor
        << " hits=" << hits.size() << "\n";
    out << "x\tz\triver\tcave\tscore\tratio(%)\n";

    out << std::fixed << std::setprecision(2);
    for (const Hit &hit: hits)
    {
        const double ratio = static_cast<double>(hit.score) / kFullRingArea * 100.0;
        out << hit.x << '\t' << hit.z << '\t' << hit.river << '\t' << hit.cave << '\t'
            << hit.score << '\t' << ratio << '\n';
    }
    return out.str();
}

} // namespace

int main(int argc, char **argv)
{
    Options options = parseOptions(argc, argv);

    if (options.help)
    {
        printUsage(std::cout);
        return 0;
    }
    if (options.version)
    {
        std::cout << "riverfinder-cli " << kCliVersion
                  << " (cubiomes MC_26_3=" << MC_26_3 << ")\n";
        return 0;
    }

    if (!options.seedSet)
        usageError("--seed is required");
    if (options.square > 0 && options.boundsSet)
        usageError("--square cannot be combined with explicit --min-x/--max-x/--min-z/--max-z");

    if (options.square > 0)
    {
        const int min = -(options.square / 2);
        options.minX = min;
        options.maxX = min + options.square - 1;
        options.minZ = min;
        options.maxZ = min + options.square - 1;
    }

    if (options.minX > options.maxX)
        usageError("--min-x must be <= --max-x");
    if (options.minZ > options.maxZ)
        usageError("--min-z must be <= --max-z");
    if (options.minX < INT_MIN + kRingOuter || options.maxX > INT_MAX - kRingOuter
        || options.minZ < INT_MIN + kRingOuter || options.maxZ > INT_MAX - kRingOuter)
        usageError("search bounds are too close to the int32 limit");

    const long long rawWidth = static_cast<long long>(options.maxX) - options.minX + 1;
    const long long rawHeight = static_cast<long long>(options.maxZ) - options.minZ + 1;
    if (rawWidth <= 0 || rawHeight <= 0)
        usageError("search area must be non-empty");
    if (rawWidth > kMaxSearchArea / rawHeight)
        usageError("search area is too large (maximum 30000000 x 30000000 blocks)");
    if (rawWidth * rawHeight > kMaxSearchArea)
        usageError("search area is too large (maximum 30000000 x 30000000 blocks)");

    const long long fullWidth = rawWidth + 2LL * kRingOuter;
    const long long fullHeight = rawHeight + 2LL * kRingOuter;
    if (fullWidth > INT_MAX || fullHeight > INT_MAX)
        usageError("search area is too large for the native API");

    if (options.threads <= 0)
    {
        const unsigned int hardware = std::thread::hardware_concurrency();
        options.threads = static_cast<int>(std::min<unsigned int>(32u, hardware == 0 ? 1u : hardware));
    }

    const int startX = options.minX - kRingOuter;
    const int startZ = options.minZ - kRingOuter;
    const int width = static_cast<int>(fullWidth);
    const int height = static_cast<int>(fullHeight);

    std::ofstream outFile;
    if (!options.outPath.empty())
    {
        outFile.open(options.outPath, std::ios::out | std::ios::trunc);
        if (!outFile)
            runtimeError("cannot open --out file: " + options.outPath);
    }

    std::cerr << "searching seed=" << options.seed
              << " mc=" << mcDisplayName(options.mc)
              << " area=[" << options.minX << "," << options.maxX << "]x["
              << options.minZ << "," << options.maxZ << "]"
              << " y=" << options.y
              << " threads=" << options.threads
              << " minArea=" << options.minArea
              << " caveFactor=" << options.caveFactor << "\n";

    const auto started = std::chrono::steady_clock::now();
    const float caveFactorF = static_cast<float>(options.caveFactor);
    const double scoreCaveFactor = static_cast<double>(caveFactorF);
    int *raw = nullptr;
    try
    {
        raw = riverSearchEx(options.seed, options.mc, startX, startZ, width, height, options.y,
                            options.minArea, options.threads, caveFactorF);
    }
    catch (const std::exception &error)
    {
        runtimeError(std::string("search failed: ") + error.what());
    }
    catch (...)
    {
        runtimeError("search failed with an unknown error");
    }

    size_t count = 0;
    std::vector<Hit> hits;
    if (raw != nullptr)
    {
        for (size_t i = 0;; ++i)
        {
            const int *quad = raw + i * 4;
            if (quad[0] == 0 && quad[1] == 0 && quad[2] == 0 && quad[3] == 0)
                break;
            ++count;
        }
        hits = filterHits(raw, count, scoreCaveFactor, options.limit);
        std::free(raw);
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    const std::string report = formatReport(options, startX, startZ, width, height, hits);

    std::cout << report;
    if (outFile)
    {
        outFile << report;
        if (!outFile)
            runtimeError("failed to write --out file: " + options.outPath);
    }

    std::cerr << "done in " << elapsed.count() << " ms, " << hits.size() << " hit(s)";
    if (!options.outPath.empty())
        std::cerr << ", report written to " << options.outPath;
    std::cerr << "\n";

    return 0;
}
