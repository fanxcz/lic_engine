#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include <lic/assets/gltf.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lic::assets {
namespace {
constexpr std::size_t kMaxVertices = 2'000'000;
constexpr std::size_t kMaxIndices = 6'000'000;

struct DataDeleter { void operator()(cgltf_data* data) const noexcept { cgltf_free(data); } };
using DataPtr = std::unique_ptr<cgltf_data, DataDeleter>;

std::string error_name(cgltf_result result) {
    switch (result) {
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    default: return "unknown cgltf error";
    }
}
DataPtr parse(const std::filesystem::path& path, bool loadBuffers) {
    if (!is_gltf_path(path) || !std::filesystem::is_regular_file(path))
        throw std::runtime_error("Expected an existing .gltf/.glb file: " + path.string());
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (!ec && size > 512ULL * 1024ULL * 1024ULL)
        throw std::runtime_error("glTF file exceeds 512 MiB: " + path.string());
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    const std::string filename = path.string();
    cgltf_result result = cgltf_parse_file(&options, filename.c_str(), &raw);
    if (result != cgltf_result_success)
        throw std::runtime_error("Could not parse glTF: " + error_name(result));
    DataPtr data(raw);
    if (loadBuffers) {
        result = cgltf_load_buffers(&options, data.get(), filename.c_str());
        if (result != cgltf_result_success)
            throw std::runtime_error("Could not load glTF buffers: " + error_name(result));
        result = cgltf_validate(data.get());
        if (result != cgltf_result_success)
            throw std::runtime_error("glTF validation failed: " + error_name(result));
    }
    return data;
}
std::array<float, 3> point(const float m[16], const float p[3]) {
    return {m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12],
            m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13],
            m[2]*p[0]+m[6]*p[1]+m[10]*p[2]+m[14]};
}
std::array<float, 3> normal(const float m[16], const float n[3]) {
    std::array<float, 3> result{m[0]*n[0]+m[4]*n[1]+m[8]*n[2],
                                m[1]*n[0]+m[5]*n[1]+m[9]*n[2],
                                m[2]*n[0]+m[6]*n[1]+m[10]*n[2]};
    const float len = std::sqrt(result[0]*result[0]+result[1]*result[1]+result[2]*result[2]);
    if (len > 0.000001F) for (float& v : result) v /= len;
    else result = {0.0F,1.0F,0.0F};
    return result;
}
void append_mesh(const cgltf_node& node, GltfAsset& asset) {
    if (node.mesh == nullptr) return;
    float matrix[16]{};
    cgltf_node_transform_world(&node, matrix);
    const cgltf_mesh& mesh = *node.mesh;
    for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
        const cgltf_primitive& primitive = mesh.primitives[p];
        if (primitive.type != cgltf_primitive_type_triangles)
            throw std::runtime_error("Only triangle-list glTF primitives are supported");
        const cgltf_accessor* positions = nullptr;
        const cgltf_accessor* normals = nullptr;
        const cgltf_accessor* uv = nullptr;
        for (cgltf_size a=0; a<primitive.attributes_count; ++a) {
            const auto& attr=primitive.attributes[a];
            if (attr.index != 0) continue;
            if (attr.type==cgltf_attribute_type_position) positions=attr.data;
            if (attr.type==cgltf_attribute_type_normal) normals=attr.data;
            if (attr.type==cgltf_attribute_type_texcoord) uv=attr.data;
        }
        if (positions==nullptr || positions->type!=cgltf_type_vec3 || positions->count<3)
            throw std::runtime_error("glTF primitive has no valid POSITION accessor");
        const auto vertexCount=static_cast<std::size_t>(positions->count);
        if (vertexCount>kMaxVertices || asset.vertices.size()>kMaxVertices-vertexCount)
            throw std::runtime_error("glTF exceeds the vertex import limit");
        const std::size_t base=asset.vertices.size();
        asset.vertices.resize(base+vertexCount);
        for (cgltf_size v=0; v<positions->count; ++v) {
            float value[3]{};
            if (!cgltf_accessor_read_float(positions,v,value,3)) throw std::runtime_error("Invalid POSITION data");
            auto& out=asset.vertices[base+static_cast<std::size_t>(v)];
            out.position=point(matrix,value);
            if (normals!=nullptr && v<normals->count) {
                float n[3]{};
                if (!cgltf_accessor_read_float(normals,v,n,3)) throw std::runtime_error("Invalid NORMAL data");
                out.normal=normal(matrix,n);
            } else out.normal={0.0F,0.0F,0.0F};
            if (uv!=nullptr && v<uv->count) {
                float tex[2]{};
                if (!cgltf_accessor_read_float(uv,v,tex,2)) throw std::runtime_error("Invalid TEXCOORD_0 data");
                out.texcoord={tex[0],tex[1]};
            }
        }
        const std::size_t first=asset.indices.size();
        const cgltf_size count=primitive.indices ? primitive.indices->count : positions->count;
        if (count<3 || count%3!=0 || count>kMaxIndices || asset.indices.size()>kMaxIndices-static_cast<std::size_t>(count))
            throw std::runtime_error("Invalid or excessive triangle index count");
        for (cgltf_size i=0;i<count;++i) {
            const cgltf_size index=primitive.indices ? cgltf_accessor_read_index(primitive.indices,i) : i;
            if (index>=positions->count) throw std::runtime_error("glTF mesh index is out of range");
            asset.indices.push_back(static_cast<std::uint32_t>(base+static_cast<std::size_t>(index)));
        }
        if (normals==nullptr) {
            for (std::size_t i=first;i<asset.indices.size();i+=3) {
                auto& a=asset.vertices[asset.indices[i]];
                auto& b=asset.vertices[asset.indices[i+1]];
                auto& c=asset.vertices[asset.indices[i+2]];
                const float x1=b.position[0]-a.position[0], y1=b.position[1]-a.position[1], z1=b.position[2]-a.position[2];
                const float x2=c.position[0]-a.position[0], y2=c.position[1]-a.position[1], z2=c.position[2]-a.position[2];
                const std::array<float,3> face{y1*z2-z1*y2,z1*x2-x1*z2,x1*y2-y1*x2};
                for (const auto ix : {asset.indices[i],asset.indices[i+1],asset.indices[i+2]})
                    for (std::size_t axis=0;axis<3;++axis) asset.vertices[ix].normal[axis]+=face[axis];
            }
            for (std::size_t i=base;i<asset.vertices.size();++i) {
                auto& n=asset.vertices[i].normal;
                const float len=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
                if (len>0.000001F) for (float& v:n) v/=len; else n={0.0F,1.0F,0.0F};
            }
        }
        lic::world::Appearance surface;
        surface.color={1.0F,1.0F,1.0F,1.0F};
        surface.metallic=1.0F;
        surface.roughness=1.0F;
        std::string materialName="Default";
        if (primitive.material) {
            const auto& material=*primitive.material;
            if (material.name) materialName=material.name;
            const auto& pbr=material.pbr_metallic_roughness;
            for (std::size_t c=0;c<4;++c) surface.color[c]=pbr.base_color_factor[c];
            surface.metallic=pbr.metallic_factor;
            surface.roughness=std::clamp(pbr.roughness_factor,0.04F,1.0F);
            surface.emission_strength=std::clamp(std::max({material.emissive_factor[0],material.emissive_factor[1],material.emissive_factor[2]}),0.0F,4.0F);
        }
        asset.primitives.push_back({static_cast<std::uint32_t>(first),
                                   static_cast<std::uint32_t>(asset.indices.size()-first),
                                   std::move(materialName),surface});
    }
}
void visit_node(const cgltf_node& node, GltfAsset& asset) {
    append_mesh(node,asset);
    for (cgltf_size i=0;i<node.children_count;++i) visit_node(*node.children[i],asset);
}
void add_uri(const char* uri,std::vector<std::filesystem::path>& out) {
    if (!uri || *uri=='\0' || std::string_view(uri).starts_with("data:")) return;
    const std::string_view value(uri);
    if (value.find("://")!=std::string_view::npos || value.starts_with("file:"))
        throw std::runtime_error("Remote glTF dependencies are not supported");
    const std::filesystem::path path{std::string(value)};
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        throw std::runtime_error("Absolute glTF dependency paths are not allowed");
    const auto safe=path.lexically_normal();
    for (const auto& part:safe) if (part=="..") throw std::runtime_error("glTF dependency traversal is not allowed");
    if (safe.empty() || safe==".") throw std::runtime_error("Empty glTF dependency path");
    if (std::find(out.begin(),out.end(),safe)==out.end()) out.push_back(safe);
}
} // namespace

bool is_gltf_path(const std::filesystem::path& path) noexcept {
    std::string ext=path.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return ext==".gltf" || ext==".glb";
}
GltfAsset load_gltf(const std::filesystem::path& path) {
    auto data=parse(path,true);
    GltfAsset asset;
    asset.name=path.stem().string();
    if (data->scene) {
        for (cgltf_size i=0;i<data->scene->nodes_count;++i) visit_node(*data->scene->nodes[i],asset);
    } else if (data->scenes_count>0 && data->scenes[0].nodes_count>0) {
        for (cgltf_size i=0;i<data->scenes[0].nodes_count;++i) visit_node(*data->scenes[0].nodes[i],asset);
    } else {
        for (cgltf_size i=0;i<data->nodes_count;++i) if (!data->nodes[i].parent) visit_node(data->nodes[i],asset);
    }
    if (asset.vertices.empty() || asset.indices.empty() || asset.primitives.empty())
        throw std::runtime_error("glTF contains no supported triangle mesh: "+path.string());
    return asset;
}
std::vector<std::filesystem::path> gltf_external_dependencies(const std::filesystem::path& path) {
    auto data=parse(path,false);
    std::vector<std::filesystem::path> result;
    for (cgltf_size i=0;i<data->buffers_count;++i) add_uri(data->buffers[i].uri,result);
    for (cgltf_size i=0;i<data->images_count;++i) add_uri(data->images[i].uri,result);
    return result;
}
} // namespace lic::assets
