#pragma once

#include "model/model_layer.hpp"

#include <cgltf.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ote {

bool codecSupported(const char* extension);
bool decodeMeshopt(cgltf_data* data);
bool decodeDracoPrimitive(const cgltf_data* data, const cgltf_primitive& prim, std::vector<float>& positions, std::vector<float>& normals,
                          std::vector<float>& texcoords, std::vector<float>& colors, std::vector<std::uint32_t>& indices);
bool decodeDracoPoints(const std::uint8_t* data, std::size_t size, const std::map<std::string, int>& properties, std::vector<float>& positions,
                       std::vector<std::uint8_t>& rgba);
bool transcodeKtx2(const std::uint8_t* data, std::size_t size, CpuImage& out);
void setCompressedTextureSupport(bool bc7, bool astc, bool etc2);

}
