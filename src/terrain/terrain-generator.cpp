// Table-driven Transvoxel surface extraction; transition strips join fine/coarse contours on exact node faces.
#include "terrain/terrain-generator.h"
#include "core/engine-error.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <memory_resource>

namespace ofg::terrain {
namespace {
// Upstream tables and notices are preserved unchanged; no symbols escape this translation unit.
#include "../../external/transvoxel/Transvoxel.cpp"

// Charges every sample/cache allocation to one bounded worker-local scratch resource.
// Output vectors have their own byteLimit reservation and transfer without retaining this resource.
class ScratchMemory : public std::pmr::memory_resource
{
    size_t m_bytes = 0;
    // Reserves requested bytes before handing them to the system allocator; failure leaves accounting unchanged.
    void* do_allocate(size_t bytes, size_t alignment) override
    {
        if (bytes > (16ull << 20) - m_bytes)
        {
            throw EngineError("Terrain worker scratch exceeds its 16 MiB limit.");
        }
        void* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        m_bytes += bytes;
        return result;
    }
    // Releases exactly the bytes recorded by the standard container allocator.
    void do_deallocate(void* pointer, size_t bytes, size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        m_bytes -= bytes;
    }
    // Scratch storage can only be deallocated through its original job's resource.
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

// Mixes integer global lattice coordinates without first converting a distant world position to float.
uint64_t hash(uint64_t value)
{
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

// Returns one deterministic corner value in [-1,1]; wraparound hashing is intentional.
double lattice(uint64_t x, uint64_t z, uint64_t seed)
{
    return double(hash(hash(x + seed) ^ hash(z + 0x9e3779b97f4a7c15ull)) >> 11) * 0x1p-52 - 1;
}

// Gives value noise continuous first derivatives at lattice boundaries.
double smooth(double x)
{
    return x * x * x * (x * (x * 6 - 15) + 10);
}

// Evaluates one bilinear octave; quintic interpolation is monotone between its integer lattice boundaries.
double octaveHeight(CellAddress cell, double x, double z, uint64_t frequency, uint64_t seed)
{
    const auto ix = int64_t(std::floor(x)), iz = int64_t(std::floor(z));
    const auto gx = uint64_t(cell.x) * frequency + uint64_t(ix);
    const auto gz = uint64_t(cell.z) * frequency + uint64_t(iz);
    const double tx = smooth(x - double(ix)), tz = smooth(z - double(iz));
    const double a = std::lerp(lattice(gx, gz, seed), lattice(gx + 1, gz, seed), tx);
    const double b = std::lerp(lattice(gx, gz + 1, seed), lattice(gx + 1, gz + 1, seed), tx);
    return std::lerp(a, b, tz);
}

// Bounds every height, not just the mesh samples. Each octave's extrema lie on the corners of the
// node rectangle split at integer noise-lattice boundaries. Summing independent extrema is conservative.
std::array<double, 2> heightRange(NodeAddress node, const GeneratorSettings& settings)
{
    double minimum = 0, maximum = 0, normalization = 0, weight = 1;
    for (unsigned octave = 0; octave < 4; ++octave)
    {
        const uint64_t frequency = uint64_t(1) << octave;
        const double width = std::ldexp(double(frequency), -node.depth);
        const double x0 = node.x * width, x1 = x0 + width;
        const double z0 = node.z * width, z1 = z0 + width;
        double low = 1, high = -1;
        for (double z = z0;; z = std::min(z1, std::floor(z) + 1))
        {
            for (double x = x0;; x = std::min(x1, std::floor(x) + 1))
            {
                const double value = octaveHeight(node.cell, x, z, frequency, settings.seed);
                low = std::min(low, value);
                high = std::max(high, value);
                if (x == x1)
                {
                    break;
                }
            }
            if (z == z1)
            {
                break;
            }
        }
        minimum += weight * low;
        maximum += weight * high;
        normalization += weight;
        weight *= .5;
    }
    // Round outward well beyond double arithmetic error; strict exclusion preserves exact-zero surfaces.
    const double margin = (1 + std::abs(settings.heightOffset) + settings.amplitude) * 1e-12;
    return {
        settings.heightOffset + settings.amplitude * minimum / normalization - margin,
        settings.heightOffset + settings.amplitude * maximum / normalization + margin
    };
}

// Rotates a face-space 3x3 grid into the requested node face, with inward face-space Z.
std::array<int, 3> facePoint(int u, int v, int face, int n)
{
    switch (face)
    {
    case 0:
        return {0, u, v};
    case 1:
        return {n, v, u};
    case 2:
        return {v, 0, u};
    case 3:
        return {u, n, v};
    case 4:
        return {u, v, 0};
    default:
        return {v, u, n};
    }
}
} // namespace

size_t TerrainGeometry::allocatedBytes() const noexcept
{
    return vertices.capacity() * sizeof(Vertex) + indices.capacity() * sizeof(uint32_t);
}

double terrainHeight(CellAddress cell, double x, double z, const GeneratorSettings& s)
{
    double sum = 0, weight = 1, normalization = 0;
    for (unsigned octave = 0; octave < 4; ++octave)
    {
        const uint64_t frequency = uint64_t(1) << octave;
        const double lx = x / s.rootWidth * double(frequency), lz = z / s.rootWidth * double(frequency);
        sum += weight * octaveHeight(cell, lx, lz, frequency, s.seed);
        normalization += weight;
        weight *= .5;
    }
    return s.heightOffset + s.amplitude * sum / normalization;
}

void validateGeometry(const TerrainGeometry& geometry, double width, size_t limit)
{
    if (!std::isfinite(width) || width <= 0 || geometry.allocatedBytes() > limit || geometry.indices.size() % 3)
    {
        throw EngineError("Invalid terrain geometry dimensions, output size or triangle count.");
    }
    if (geometry.certifiedEmpty && (!geometry.vertices.empty() || !geometry.indices.empty()))
    {
        throw EngineError("Certified empty terrain contains geometry.");
    }
    const double tolerance = width * 1e-6;
    for (const auto& vertex : geometry.vertices)
    {
        for (float value : {vertex.position.x, vertex.position.y, vertex.position.z})
        {
            if (!std::isfinite(value) || value < -tolerance || value > width + tolerance)
            {
                throw EngineError("Terrain vertex leaves its node bounds.");
            }
        }
        if (!std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z))
        {
            throw EngineError("Terrain normal is not finite.");
        }
    }
    for (auto index : geometry.indices)
    {
        if (index >= geometry.vertices.size())
        {
            throw EngineError("Terrain triangle index is out of range.");
        }
    }
}

struct TerrainMesher::Work
{
    NodeAddress address;
    GeneratorSettings settings;
    size_t byteLimit;
    uint8_t faces;
    int n;
    double width, spacing;
    std::array<double, 3> origin;
    TerrainGeometry geometry;
    ScratchMemory scratch;
    std::pmr::vector<double> samples{&scratch};
    std::pmr::unordered_map<uint64_t, uint32_t> vertices{&scratch};
    uint32_t cell = 0;
    int face = -1;
    bool done = false, taken = false;

    // Creates one bounded sample lattice; conservative global height bounds certify empty volumes.
    Work(NodeAddress a, uint8_t mask, GeneratorSettings s, size_t limit)
        : address(a)
        , settings(s)
        , byteLimit(limit)
        , faces(mask)
        , n(int(s.intervals))
        , width(std::ldexp(s.rootWidth, -a.depth))
        , spacing(width / s.intervals)
        , origin{a.x * width, a.y * width, a.z * width}
    {
        const double y = double(a.cell.y) * s.rootWidth + origin[1];
        if (y > s.heightOffset + s.amplitude || y + width < s.heightOffset - s.amplitude)
        {
            geometry.certifiedEmpty = true;
            done = true;
            return;
        }
        const auto range = heightRange(a, s);
        if (y > range[1] || y + width < range[0])
        {
            geometry.certifiedEmpty = true;
            done = true;
            return;
        }
        samples.resize(size_t(n + 1) * (n + 1) * (n + 1));
        // A height surface allows one noise evaluation per X/Z column, independent of volume height.
        for (int z = 0; z <= n; ++z)
        {
            for (int x = 0; x <= n; ++x)
            {
                const double h = terrainHeight(a.cell, origin[0] + x * spacing, origin[2] + z * spacing, s);
                for (int iy = 0; iy <= n; ++iy)
                {
                    samples[pointId({x, iy, z})] = h - (y + iy * spacing);
                }
            }
        }
    }

    // Gives a sample's compact local lattice identity, also used in canonical edge keys.
    uint32_t pointId(std::array<int, 3> p) const { return uint32_t(p[0] + (n + 1) * (p[1] + (n + 1) * p[2])); }

    // Grows one output vector without exceeding the combined output-capacity reservation.
    void reserveVertex()
    {
        if (geometry.vertices.size() == geometry.vertices.capacity())
        {
            const size_t available = (byteLimit - geometry.indices.capacity() * sizeof(uint32_t)) / sizeof(Vertex);
            const size_t capacity = std::min(available, std::max(size_t(64), geometry.vertices.capacity() * 2));
            if (capacity <= geometry.vertices.size())
            {
                throw EngineError("Terrain vertex output exceeds its reserved byte limit.");
            }
            geometry.vertices.reserve(capacity);
        }
    }

    // Canonicalizes endpoints so adjacent cells interpolate a shared edge in the same order.
    uint32_t vertex(std::array<int, 3> a, std::array<int, 3> b)
    {
        auto ia = pointId(a), ib = pointId(b);
        if (ib < ia)
        {
            std::swap(a, b);
            std::swap(ia, ib);
        }
        const double da = samples[ia], db = samples[ib];
        if (da == db)
        {
            throw EngineError("Transvoxel table selected an edge without a density crossing.");
        }
        // Exact zero endpoints use a point identity, avoiding duplicate topology at lattice corners.
        const uint64_t key = da == 0   ? (uint64_t(ia) << 32 | ia)
                             : db == 0 ? (uint64_t(ib) << 32 | ib)
                                       : (uint64_t(ia) << 32 | ib);
        if (const auto found = vertices.find(key); found != vertices.end())
        {
            return found->second;
        }
        const double t = da / (da - db);
        const double px = std::lerp(double(a[0]), double(b[0]), t) * spacing;
        const double py = std::lerp(double(a[1]), double(b[1]), t) * spacing;
        const double pz = std::lerp(double(a[2]), double(b[2]), t) * spacing;
        const double x = origin[0] + px, z = origin[2] + pz;
        const double dx =
            (terrainHeight(address.cell, x + 1, z, settings) - terrainHeight(address.cell, x - 1, z, settings)) * .5;
        const double dz =
            (terrainHeight(address.cell, x, z + 1, settings) - terrainHeight(address.cell, x, z - 1, settings)) * .5;
        const double inverseLength = 1 / std::sqrt(dx * dx + 1 + dz * dz);
        Vertex value{};
        value.position = {float(px), float(py), float(pz)};
        value.normal = {float(-dx * inverseLength), float(inverseLength), float(-dz * inverseLength)};
        value.uv = {float(px / width), float(pz / width)};
        const uint32_t index = uint32_t(geometry.vertices.size());
        reserveVertex();
        geometry.vertices.push_back(value);
        vertices.emplace(key, index);
        return index;
    }

    // Omits zero-area triangles caused by exact-zero samples; aligns winding with the outward height normal.
    void triangle(uint32_t a, uint32_t b, uint32_t c)
    {
        if (a == b || b == c || a == c)
        {
            return;
        }
        const auto& va = geometry.vertices[a];
        const auto& vb = geometry.vertices[b];
        const auto& vc = geometry.vertices[c];
        const auto normal = math::cross(math::sub(vb.position, va.position), math::sub(vc.position, va.position));
        if (math::lengthSquared(normal) == 0)
        {
            return;
        }
        if (math::dot(normal, va.normal) < 0)
        {
            std::swap(b, c);
        }
        if (geometry.indices.capacity() < geometry.indices.size() + 3)
        {
            const size_t available = (byteLimit - geometry.vertices.capacity() * sizeof(Vertex)) / sizeof(uint32_t);
            const size_t capacity = std::min(available, std::max(size_t(96), geometry.indices.capacity() * 2));
            if (capacity < geometry.indices.size() + 3)
            {
                throw EngineError("Terrain index output exceeds its reserved byte limit.");
            }
            geometry.indices.reserve(capacity);
        }
        geometry.indices.insert(geometry.indices.end(), {a, b, c});
    }

    // Emits one regular cell from Lengyel's modified Marching Cubes table.
    void regular(uint32_t index)
    {
        const int x = index % n, y = (index / n) % n, z = index / (n * n);
        std::array<std::array<int, 3>, 8> points;
        unsigned code = 0;
        for (unsigned i = 0; i < 8; ++i)
        {
            points[i] = {x + int(i & 1), y + int((i >> 1) & 1), z + int((i >> 2) & 1)};
            code |= unsigned(samples[pointId(points[i])] < 0) << i;
        }
        const auto& data = regularCellData[regularCellClass[code]];
        std::array<uint32_t, 12> indices;
        for (int i = 0; i < data.GetVertexCount(); ++i)
        {
            const auto edge = regularVertexData[code][i];
            indices[i] = vertex(points[(edge >> 4) & 15], points[edge & 15]);
        }
        for (int i = 0; i < data.GetTriangleCount(); ++i)
        {
            triangle(
                indices[data.vertexIndex[3 * i]],
                indices[data.vertexIndex[3 * i + 1]],
                indices[data.vertexIndex[3 * i + 2]]
            );
        }
    }

    // Emits a fine-to-coarse strip on a shared face; both sides reuse the same global endpoint samples.
    void transition(uint32_t index)
    {
        const int u = 2 * (index % (n / 2)), v = 2 * (index / (n / 2));
        std::array<std::array<int, 3>, 13> points;
        for (int i = 0; i < 9; ++i)
        {
            points[i] = facePoint(u + i % 3, v + i / 3, face, n);
        }
        points[9] = points[0];
        points[10] = points[2];
        points[11] = points[6];
        points[12] = points[8];
        constexpr std::array<int, 9> order{0, 1, 2, 5, 8, 7, 6, 3, 4};
        unsigned code = 0;
        for (unsigned i = 0; i < order.size(); ++i)
        {
            code |= unsigned(samples[pointId(points[order[i]])] < 0) << i;
        }
        const auto& data = transitionCellData[transitionCellClass[code] & 127];
        std::array<uint32_t, 12> indices;
        for (int i = 0; i < data.GetVertexCount(); ++i)
        {
            const auto edge = transitionVertexData[code][i];
            indices[i] = vertex(points[(edge >> 4) & 15], points[edge & 15]);
        }
        for (int i = 0; i < data.GetTriangleCount(); ++i)
        {
            triangle(
                indices[data.vertexIndex[3 * i]],
                indices[data.vertexIndex[3 * i + 1]],
                indices[data.vertexIndex[3 * i + 2]]
            );
        }
    }
};

TerrainMesher::TerrainMesher(NodeAddress address, uint8_t faces, GeneratorSettings settings, size_t byteLimit)
{
    validateAddress(address);
    if (!std::isfinite(settings.rootWidth) || settings.rootWidth <= 0 || !std::isfinite(settings.heightOffset) ||
        !std::isfinite(settings.amplitude) || settings.amplitude < 0 || settings.intervals < 2 ||
        settings.intervals > 64 || (settings.intervals & (settings.intervals - 1)) || faces > 63 || byteLimit < 1024)
    {
        throw EngineError("Invalid terrain generator settings.");
    }
    m_work = std::make_unique<Work>(address, faces, settings, byteLimit);
}

TerrainMesher::~TerrainMesher() = default;

bool TerrainMesher::step(uint32_t budget)
{
    auto& w = *m_work;
    while (budget-- && !w.done)
    {
        if (w.face < 0)
        {
            w.regular(w.cell++);
            if (w.cell == uint32_t(w.n * w.n * w.n))
            {
                w.face = 0;
                w.cell = 0;
            }
        }
        else
        {
            while (w.face < 6 && !(w.faces & (1u << w.face)))
            {
                ++w.face;
            }
            if (w.face == 6)
            {
                w.done = true;
                break;
            }
            w.transition(w.cell++);
            if (w.cell == uint32_t(w.n * w.n / 4))
            {
                ++w.face;
                w.cell = 0;
            }
        }
    }
    return w.done;
}

TerrainGeometry TerrainMesher::takeGeometry()
{
    if (!m_work->done || m_work->taken)
    {
        throw EngineError("Terrain output is unavailable or already taken.");
    }
    validateGeometry(m_work->geometry, m_work->width, m_work->byteLimit);
    m_work->taken = true;
    return std::move(m_work->geometry);
}
} // namespace ofg::terrain
