#pragma once

#include <madrona/dyn_array.hpp>
#include <madrona/math.hpp>
#include <madrona/span.hpp>
#include <madrona/optional.hpp>

#include <string_view>

namespace madrona {
namespace imp {

struct SourceVertex {
    math::Vector3 position;
    math::Vector3 normal;
    math::Vector4 tangentAndSign;
    math::Vector2 uv;
};

struct SourceMesh {
    const math::Vector3 *positions;
    const math::Vector3 *normals;
    const math::Vector4 *tangentAndSigns;
    const math::Vector2 *uvs;
    const uint32_t *indices;
    const uint32_t *faceCounts;
    uint32_t numVertices;
    uint32_t numFaces;
    uint32_t materialIDX;
};

struct SourceObject {
    Span<const SourceMesh> meshes;
};

struct ImportedObject {
    DynArray<DynArray<math::Vector3>> positionArrays;
    DynArray<DynArray<math::Vector3>> normalArrays;
    DynArray<DynArray<math::Vector4>> tangentAndSignArrays;
    DynArray<DynArray<math::Vector2>> uvArrays;
    DynArray<DynArray<uint32_t>> indexArrays;
    DynArray<DynArray<uint32_t>> faceCountArrays;

    DynArray<SourceMesh> meshes;
};

struct ImportedMaterial {};

struct ImportedInstance {
    math::Mat3x4 txfm;
    uint32_t objIDX;
};

struct ImportedAssets {
    DynArray<ImportedObject> objects;
    DynArray<ImportedMaterial> materials;
    DynArray<ImportedInstance> instances;

    static Optional<ImportedAssets> importFromDisk(const char *path);
};

}
}
