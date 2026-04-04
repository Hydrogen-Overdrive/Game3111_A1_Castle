#include "SkullTxtMesh.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {

struct SkullVertex {
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 TexC;
};

static std::string Trim(const std::string& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

static bool ParseSkullTxt(std::ifstream& fin, std::vector<SkullVertex>& outVerts, std::vector<uint32_t>& outIdx)
{
    std::string line;
    int vCount = 0;
    int tCount = 0;

    while (std::getline(fin, line)) {
        std::string t = Trim(line);
        if (t.rfind("VertexCount:", 0) == 0) {
            if (sscanf_s(t.c_str(), "VertexCount: %d", &vCount) != 1)
                return false;
        }
        else if (t.rfind("TriangleCount:", 0) == 0) {
            if (sscanf_s(t.c_str(), "TriangleCount: %d", &tCount) != 1)
                return false;
        }
    }

    if (vCount <= 0 || tCount <= 0)
        return false;

    fin.clear();
    fin.seekg(0);

    while (std::getline(fin, line)) {
        if (line.find("VertexList") != std::string::npos)
            break;
    }

    while (std::getline(fin, line)) {
        if (Trim(line) == "{")
            break;
    }

    outVerts.reserve(static_cast<size_t>(vCount));
    for (int i = 0; i < vCount; ++i) {
        if (!std::getline(fin, line))
            return false;
        SkullVertex v{};
        std::string tl = Trim(line);
        if (tl == "}")
            return false;
        if (sscanf_s(
                tl.c_str(),
                "%f %f %f %f %f %f",
                &v.Pos.x,
                &v.Pos.y,
                &v.Pos.z,
                &v.Normal.x,
                &v.Normal.y,
                &v.Normal.z) != 6)
            return false;
        outVerts.push_back(v);
    }

    while (std::getline(fin, line)) {
        std::string u = Trim(line);
        if (u == "}")
            break;
    }

    while (std::getline(fin, line)) {
        if (line.find("TriangleList") != std::string::npos)
            break;
    }

    while (std::getline(fin, line)) {
        if (Trim(line) == "{")
            break;
    }

    outIdx.reserve(static_cast<size_t>(tCount) * 3u);
    for (int i = 0; i < tCount; ++i) {
        if (!std::getline(fin, line))
            return false;
        unsigned a = 0, b = 0, c = 0;
        std::string tl = Trim(line);
        if (tl == "}")
            return false;
        if (sscanf_s(tl.c_str(), "%u %u %u", &a, &b, &c) != 3)
            return false;
        outIdx.push_back(a);
        outIdx.push_back(b);
        outIdx.push_back(c);
    }

    if (outVerts.size() != static_cast<size_t>(vCount))
        return false;
    if (outIdx.size() != static_cast<size_t>(tCount) * 3u)
        return false;

    // Planar UVs from axis-aligned bounds (XZ), then center mesh: bottom at Y=0, centered on XZ.
    float minX = outVerts[0].Pos.x, maxX = outVerts[0].Pos.x;
    float minY = outVerts[0].Pos.y, maxY = outVerts[0].Pos.y;
    float minZ = outVerts[0].Pos.z, maxZ = outVerts[0].Pos.z;
    for (const auto& v : outVerts) {
        minX = (std::min)(minX, v.Pos.x);
        maxX = (std::max)(maxX, v.Pos.x);
        minY = (std::min)(minY, v.Pos.y);
        maxY = (std::max)(maxY, v.Pos.y);
        minZ = (std::min)(minZ, v.Pos.z);
        maxZ = (std::max)(maxZ, v.Pos.z);
    }
    const float dx = maxX - minX;
    const float dz = maxZ - minZ;
    const float cx = 0.5f * (minX + maxX);
    const float cz = 0.5f * (minZ + maxZ);

    for (auto& v : outVerts) {
        const float u = (dx > 1e-6f) ? (v.Pos.x - minX) / dx : 0.0f;
        const float w = (dz > 1e-6f) ? (v.Pos.z - minZ) / dz : 0.0f;
        v.TexC = XMFLOAT2(u, w);

        v.Pos.x -= cx;
        v.Pos.z -= cz;
        v.Pos.y -= minY;

        XMVECTOR n = XMVectorSet(v.Normal.x, v.Normal.y, v.Normal.z, 0.0f);
        n = XMVector3Normalize(n);
        XMStoreFloat3(&v.Normal, n);
    }

    return true;
}

} // namespace

bool BuildSkullTxtGeometry(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::wstring& filePath,
    std::unique_ptr<MeshGeometry>& outGeometry)
{
    std::ifstream fin(filePath);
    if (!fin.is_open())
        return false;

    std::vector<SkullVertex> vertices;
    std::vector<uint32_t> indices;
    if (!ParseSkullTxt(fin, vertices, indices))
        return false;

    const UINT vbByteSize = static_cast<UINT>(sizeof(SkullVertex) * vertices.size());
    const UINT ibByteSize = static_cast<UINT>(sizeof(uint32_t) * indices.size());

    ComPtr<ID3D12Resource> vbu;
    ComPtr<ID3D12Resource> ibu;

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "skullGeo";
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        device,
        cmdList,
        vertices.data(),
        vbByteSize,
        geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        device,
        cmdList,
        indices.data(),
        ibByteSize,
        geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(SkullVertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry sub;
    sub.IndexCount = static_cast<UINT>(indices.size());
    sub.StartIndexLocation = 0;
    sub.BaseVertexLocation = 0;

    float minX = vertices[0].Pos.x, maxX = vertices[0].Pos.x;
    float minY = vertices[0].Pos.y, maxY = vertices[0].Pos.y;
    float minZ = vertices[0].Pos.z, maxZ = vertices[0].Pos.z;
    for (const auto& v : vertices) {
        minX = (std::min)(minX, v.Pos.x);
        maxX = (std::max)(maxX, v.Pos.x);
        minY = (std::min)(minY, v.Pos.y);
        maxY = (std::max)(maxY, v.Pos.y);
        minZ = (std::min)(minZ, v.Pos.z);
        maxZ = (std::max)(maxZ, v.Pos.z);
    }
    sub.Bounds.Center = XMFLOAT3(0.5f * (minX + maxX), 0.5f * (minY + maxY), 0.5f * (minZ + maxZ));
    sub.Bounds.Extents = XMFLOAT3(0.5f * (maxX - minX), 0.5f * (maxY - minY), 0.5f * (maxZ - minZ));

    geo->DrawArgs["skull"] = sub;

    outGeometry = std::move(geo);
    return true;
}
