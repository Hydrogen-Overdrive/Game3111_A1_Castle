#pragma once

#include <memory>
#include <string>

#include "../../Common/d3dUtil.h"

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

// Loads Luna Week12 skull.txt (VertexList + TriangleList). Uses 32-bit indices (vertex count > 65535).
// Returns false if the file is missing or malformed.
bool BuildSkullTxtGeometry(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::wstring& filePath,
    std::unique_ptr<MeshGeometry>& outGeometry);
