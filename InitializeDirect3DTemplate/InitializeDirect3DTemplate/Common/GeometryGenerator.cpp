#include "GeometryGenerator.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace DirectX;

GeometryGenerator::MeshData GeometryGenerator::CreateBox(float width, float height, float depth, uint32 numSubdivisions)
{
    MeshData meshData;
    Vertex v[24];

    float w2 = 0.5f * width;
    float h2 = 0.5f * height;
    float d2 = 0.5f * depth;

    // Fill in the front face vertex data.
    v[0] = Vertex(-w2, -h2, -d2, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    v[1] = Vertex(-w2, +h2, -d2, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v[2] = Vertex(+w2, +h2, -d2, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    v[3] = Vertex(+w2, -h2, -d2, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);

    // Fill in the back face vertex data.
    v[4] = Vertex(-w2, -h2, +d2, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    v[5] = Vertex(+w2, -h2, +d2, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    v[6] = Vertex(+w2, +h2, +d2, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v[7] = Vertex(-w2, +h2, +d2, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    // Fill in the top face vertex data.
    v[8] = Vertex(-w2, +h2, -d2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    v[9] = Vertex(-w2, +h2, +d2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v[10] = Vertex(+w2, +h2, +d2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    v[11] = Vertex(+w2, +h2, -d2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);

    // Fill in the bottom face vertex data.
    v[12] = Vertex(-w2, -h2, -d2, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    v[13] = Vertex(+w2, -h2, -d2, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    v[14] = Vertex(+w2, -h2, +d2, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v[15] = Vertex(-w2, -h2, +d2, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    // Fill in the left face vertex data.
    v[16] = Vertex(-w2, -h2, +d2, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);
    v[17] = Vertex(-w2, +h2, +d2, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
    v[18] = Vertex(-w2, +h2, -d2, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f);
    v[19] = Vertex(-w2, -h2, -d2, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f);

    // Fill in the right face vertex data.
    v[20] = Vertex(+w2, -h2, -d2, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    v[21] = Vertex(+w2, +h2, -d2, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    v[22] = Vertex(+w2, +h2, +d2, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    v[23] = Vertex(+w2, -h2, +d2, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);

    meshData.Vertices.assign(&v[0], &v[24]);

    uint32 i[36];

    // Fill in the front face index data
    i[0] = 0; i[1] = 1; i[2] = 2;
    i[3] = 0; i[4] = 2; i[5] = 3;

    // Fill in the back face index data
    i[6] = 4; i[7] = 5; i[8] = 6;
    i[9] = 4; i[10] = 6; i[11] = 7;

    // Fill in the top face index data
    i[12] = 8; i[13] = 9; i[14] = 10;
    i[15] = 8; i[16] = 10; i[17] = 11;

    // Fill in the bottom face index data
    i[18] = 12; i[19] = 13; i[20] = 14;
    i[21] = 12; i[22] = 14; i[23] = 15;

    // Fill in the left face index data
    i[24] = 16; i[25] = 17; i[26] = 18;
    i[27] = 16; i[28] = 18; i[29] = 19;

    // Fill in the right face index data
    i[30] = 20; i[31] = 21; i[32] = 22;
    i[33] = 20; i[34] = 22; i[35] = 23;

    meshData.Indices32.assign(&i[0], &i[36]);
    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateSphere(float radius, uint32 sliceCount, uint32 stackCount)
{
    MeshData meshData;
    Vertex topVertex(0.0f, +radius, 0.0f, 0.0f, +1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Vertex bottomVertex(0.0f, -radius, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    meshData.Vertices.push_back(topVertex);
    float phiStep = XM_PI / stackCount;
    float thetaStep = 2.0f * XM_PI / sliceCount;
    for (uint32 i = 1; i <= stackCount - 1; ++i) {
        float phi = i * phiStep;
        for (uint32 j = 0; j <= sliceCount; ++j) {
            float theta = j * thetaStep;
            Vertex v;
            v.Position.x = radius * sinf(phi) * cosf(theta);
            v.Position.y = radius * cosf(phi);
            v.Position.z = radius * sinf(phi) * sinf(theta);
            XMVECTOR p = XMLoadFloat3(&v.Position);
            XMStoreFloat3(&v.Normal, XMVector3Normalize(p));
            v.TexC.x = theta / XM_2PI;
            v.TexC.y = phi / XM_PI;
            meshData.Vertices.push_back(v);
        }
    }
    meshData.Vertices.push_back(bottomVertex);
    for (uint32 i = 1; i <= sliceCount; ++i) {
        meshData.Indices32.push_back(0);
        meshData.Indices32.push_back(i + 1);
        meshData.Indices32.push_back(i);
    }
    uint32 baseIndex = 1;
    uint32 ringVertexCount = sliceCount + 1;
    for (uint32 i = 0; i < stackCount - 2; ++i) {
        for (uint32 j = 0; j < sliceCount; ++j) {
            meshData.Indices32.push_back(baseIndex + i * ringVertexCount + j);
            meshData.Indices32.push_back(baseIndex + i * ringVertexCount + j + 1);
            meshData.Indices32.push_back(baseIndex + (i + 1) * ringVertexCount + j);
            meshData.Indices32.push_back(baseIndex + (i + 1) * ringVertexCount + j);
            meshData.Indices32.push_back(baseIndex + i * ringVertexCount + j + 1);
            meshData.Indices32.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
        }
    }
    uint32 southPoleIndex = (uint32)meshData.Vertices.size() - 1;
    baseIndex = southPoleIndex - ringVertexCount;
    for (uint32 i = 0; i < sliceCount; ++i) {
        meshData.Indices32.push_back(southPoleIndex);
        meshData.Indices32.push_back(baseIndex + i);
        meshData.Indices32.push_back(baseIndex + i + 1);
    }
    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateCylinder(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount)
{
    MeshData meshData;
    float stackHeight = height / stackCount;
    float radiusStep = (topRadius - bottomRadius) / stackCount;
    uint32 ringCount = stackCount + 1;
    for (uint32 i = 0; i < ringCount; ++i) {
        float y = -0.5f * height + i * stackHeight;
        float r = bottomRadius + i * radiusStep;
        float dTheta = 2.0f * XM_PI / sliceCount;
        for (uint32 j = 0; j <= sliceCount; ++j) {
            Vertex vertex;
            float c = cosf(j * dTheta);
            float s = sinf(j * dTheta);
            vertex.Position = XMFLOAT3(r * c, y, r * s);
            vertex.TexC.x = (float)j / sliceCount;
            vertex.TexC.y = 1.0f - (float)i / stackCount;
            vertex.TangentU = XMFLOAT3(-s, 0.0f, c);
            float dr = bottomRadius - topRadius;
            XMFLOAT3 bitangent(dr * c, -height, dr * s);
            XMVECTOR T = XMLoadFloat3(&vertex.TangentU);
            XMVECTOR B = XMLoadFloat3(&bitangent);
            XMVECTOR N = XMVector3Normalize(XMVector3Cross(T, B));
            XMStoreFloat3(&vertex.Normal, N);
            meshData.Vertices.push_back(vertex);
        }
    }
    uint32 ringVertexCount = sliceCount + 1;
    for (uint32 i = 0; i < stackCount; ++i) {
        for (uint32 j = 0; j < sliceCount; ++j) {
            meshData.Indices32.push_back(i * ringVertexCount + j);
            meshData.Indices32.push_back((i + 1) * ringVertexCount + j);
            meshData.Indices32.push_back((i + 1) * ringVertexCount + j + 1);
            meshData.Indices32.push_back(i * ringVertexCount + j);
            meshData.Indices32.push_back((i + 1) * ringVertexCount + j + 1);
            meshData.Indices32.push_back(i * ringVertexCount + j + 1);
        }
    }
    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateGrid(float width, float depth, uint32 m, uint32 n)
{
    MeshData meshData;
    uint32 vertexCount = m * n;
    uint32 faceCount = (m - 1) * (n - 1) * 2;
    float halfW = 0.5f * width;
    float halfD = 0.5f * depth;
    float dx = width / (n - 1);
    float dz = depth / (m - 1);
    float du = 1.0f / (n - 1);
    float dv = 1.0f / (m - 1);
    meshData.Vertices.resize(vertexCount);
    for (uint32 i = 0; i < m; ++i) {
        float z = halfD - i * dz;
        for (uint32 j = 0; j < n; ++j) {
            float x = -halfW + j * dx;
            meshData.Vertices[i * n + j].Position = XMFLOAT3(x, 0.0f, z);
            meshData.Vertices[i * n + j].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
            meshData.Vertices[i * n + j].TangentU = XMFLOAT3(1.0f, 0.0f, 0.0f);
            meshData.Vertices[i * n + j].TexC.x = j * du;
            meshData.Vertices[i * n + j].TexC.y = i * dv;
        }
    }
    meshData.Indices32.resize(faceCount * 3);
    uint32 k = 0;
    for (uint32 i = 0; i < m - 1; ++i) {
        for (uint32 j = 0; j < n - 1; ++j) {
            meshData.Indices32[k] = i * n + j;
            meshData.Indices32[k + 1] = i * n + j + 1;
            meshData.Indices32[k + 2] = (i + 1) * n + j;
            meshData.Indices32[k + 3] = (i + 1) * n + j;
            meshData.Indices32[k + 4] = i * n + j + 1;
            meshData.Indices32[k + 5] = (i + 1) * n + j + 1;
            k += 6;
        }
    }
    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreatePyramid(float width, float height, float depth)
{
    MeshData meshData;
    float w2 = 0.5f * width;
    float d2 = 0.5f * depth;
    meshData.Vertices = {
        Vertex(0.0f, height, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f),
        Vertex(-w2, 0.0f, -d2, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        Vertex(w2, 0.0f, -d2, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f),
        Vertex(w2, 0.0f,  d2, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f),
        Vertex(-w2, 0.0f,  d2, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f)
    };
    meshData.Indices32 = { 0,2,1, 0,3,2, 0,4,3, 0,1,4, 1,2,3, 1,3,4 };
    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateWedge(float width, float height, float depth)
{
    MeshData meshData;
    float w2 = 0.5f * width;
    float h2 = 0.5f * height;
    float d2 = 0.5f * depth;
    meshData.Vertices = {
        Vertex(-w2, -h2, -d2, 0,0,-1, 1,0,0, 0,1),
        Vertex(-w2,  h2, -d2, 0,0,-1, 1,0,0, 0,0),
        Vertex(w2, -h2, -d2, 0,0,-1, 1,0,0, 1,1),
        Vertex(-w2, -h2,  d2, 0,0, 1, 1,0,0, 0,1),
        Vertex(-w2,  h2,  d2, 0,0, 1, 1,0,0, 0,0),
        Vertex(w2, -h2,  d2, 0,0, 1, 1,0,0, 1,1)
    };
    meshData.Indices32 = { 0,1,2, 3,5,4, 0,2,5, 0,5,3, 1,4,5, 1,5,2, 0,3,4, 0,4,1 };
    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateCone(float radius, float height, uint32 sliceCount)
{
    MeshData meshData;

    // Create a cone mesh (sides + base cap).
    float yBase = -0.5f * height;
    float yApex = 0.5f * height;

    const uint32 ringVertexCount = sliceCount + 1;
    for (uint32 i = 0; i < ringVertexCount; ++i)
    {
        float theta = i * (2.0f * XM_PI / sliceCount);
        float c = cosf(theta);
        float s = sinf(theta);

        XMFLOAT3 p(radius * c, yBase, radius * s);

        XMFLOAT3 n(c, radius / height, s);
        XMVECTOR nVec = XMVector3Normalize(XMLoadFloat3(&n));
        XMStoreFloat3(&n, nVec);

        XMFLOAT3 t(-s, 0.0f, c);

        float u = static_cast<float>(i) / sliceCount;
        float v = 1.0f;

        meshData.Vertices.push_back(Vertex(p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y, t.z, u, v));
    }

    uint32 apexIndex = static_cast<uint32>(meshData.Vertices.size());
    meshData.Vertices.push_back(
        Vertex(0.0f, yApex, 0.0f,
            0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.5f, 0.0f));

    for (uint32 i = 0; i < sliceCount; ++i)
    {
        meshData.Indices32.push_back(apexIndex);
        meshData.Indices32.push_back(i + 1);
        meshData.Indices32.push_back(i);
    }

    uint32 baseCenterIndex = static_cast<uint32>(meshData.Vertices.size());
    meshData.Vertices.push_back(
        Vertex(0.0f, yBase, 0.0f,
            0.0f, -1.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.5f, 0.5f));

    uint32 baseStartIndex = static_cast<uint32>(meshData.Vertices.size());
    for (uint32 i = 0; i < ringVertexCount; ++i)
    {
        float theta = i * (2.0f * XM_PI / sliceCount);
        float c = cosf(theta);
        float s = sinf(theta);

        float x = radius * c;
        float z = radius * s;
        XMFLOAT2 uv(x / (2.0f * radius) + 0.5f, z / (2.0f * radius) + 0.5f);

        meshData.Vertices.push_back(
            Vertex(x, yBase, z,
                0.0f, -1.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                uv.x, uv.y));
    }

    for (uint32 i = 0; i < sliceCount; ++i)
    {
        meshData.Indices32.push_back(baseCenterIndex);
        meshData.Indices32.push_back(baseStartIndex + i);
        meshData.Indices32.push_back(baseStartIndex + i + 1);
    }

    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateTriangularPrism(float width, float height, float depth)
{
    MeshData meshData;

    float w2 = 0.5f * width;
    float h2 = 0.5f * height;
    float d2 = 0.5f * depth;

    // Triangle in X/Y plane.
    XMFLOAT3 A0(-w2, -h2, -d2);
    XMFLOAT3 B0(+w2, -h2, -d2);
    XMFLOAT3 C0(0.0f, +h2, -d2);

    XMFLOAT3 A1(-w2, -h2, +d2);
    XMFLOAT3 B1(+w2, -h2, +d2);
    XMFLOAT3 C1(0.0f, +h2, +d2);

    auto addVertex = [&](const XMFLOAT3& p, const XMFLOAT3& n, const XMFLOAT3& t, const XMFLOAT2& uv)
    {
        meshData.Vertices.push_back(Vertex(p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y, t.z, uv.x, uv.y));
    };

    auto addTri = [&](uint32 i0, uint32 i1, uint32 i2)
    {
        meshData.Indices32.push_back(i0);
        meshData.Indices32.push_back(i1);
        meshData.Indices32.push_back(i2);
    };

    // Front triangle (+Z).
    {
        uint32 start = static_cast<uint32>(meshData.Vertices.size());
        XMFLOAT3 n(0.0f, 0.0f, 1.0f);
        XMFLOAT3 t(1.0f, 0.0f, 0.0f);
        addVertex(A1, n, t, XMFLOAT2(0.0f, 0.0f));
        addVertex(B1, n, t, XMFLOAT2(1.0f, 0.0f));
        addVertex(C1, n, t, XMFLOAT2(0.5f, 1.0f));
        addTri(start + 0, start + 2, start + 1);
    }

    // Back triangle (-Z).
    {
        uint32 start = static_cast<uint32>(meshData.Vertices.size());
        XMFLOAT3 n(0.0f, 0.0f, -1.0f);
        XMFLOAT3 t(1.0f, 0.0f, 0.0f);
        addVertex(A0, n, t, XMFLOAT2(0.0f, 0.0f));
        addVertex(C0, n, t, XMFLOAT2(0.5f, 1.0f));
        addVertex(B0, n, t, XMFLOAT2(1.0f, 0.0f));
        addTri(start + 0, start + 1, start + 2);
    }

    auto buildSideQuad = [&](const XMFLOAT3& P0, const XMFLOAT3& P1, const XMFLOAT3& P2, const XMFLOAT3& P3)
    {
        XMVECTOR p0 = XMLoadFloat3(&P0);
        XMVECTOR p1 = XMLoadFloat3(&P1);
        XMVECTOR p2 = XMLoadFloat3(&P2);

        XMVECTOR e0 = p1 - p0;
        XMVECTOR e1 = p2 - p0;
        XMVECTOR nVec = XMVector3Normalize(XMVector3Cross(e0, e1));
        XMFLOAT3 n;
        XMStoreFloat3(&n, nVec);

        XMVECTOR tVec = XMVector3Normalize(e0);
        XMFLOAT3 t;
        XMStoreFloat3(&t, tVec);

        uint32 start = static_cast<uint32>(meshData.Vertices.size());
        addVertex(P0, n, t, XMFLOAT2(0.0f, 0.0f));
        addVertex(P1, n, t, XMFLOAT2(1.0f, 0.0f));
        addVertex(P2, n, t, XMFLOAT2(1.0f, 1.0f));
        addVertex(P3, n, t, XMFLOAT2(0.0f, 1.0f));

        addTri(start + 0, start + 2, start + 1);
        addTri(start + 0, start + 3, start + 2);
    };

    // Side quads: AB, BC, CA.
    buildSideQuad(A0, B0, B1, A1);
    buildSideQuad(B0, C0, C1, B1);
    buildSideQuad(C0, A0, A1, C1);

    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateDiamond(float width, float height)
{
    MeshData meshData;

    float w2 = 0.5f * width;
    float h2 = 0.5f * height;

    const XMFLOAT3 n(0.0f, 1.0f, 0.0f);
    const XMFLOAT3 t(1.0f, 0.0f, 0.0f);

    meshData.Vertices = {
        Vertex(0.0f, 0.0f, +h2, n.x, n.y, n.z, t.x, t.y, t.z, 0.5f, 0.0f),  // top
        Vertex(+w2, 0.0f, 0.0f, n.x, n.y, n.z, t.x, t.y, t.z, 1.0f, 0.5f),  // right
        Vertex(0.0f, 0.0f, -h2, n.x, n.y, n.z, t.x, t.y, t.z, 0.5f, 1.0f),  // bottom
        Vertex(-w2, 0.0f, 0.0f, n.x, n.y, n.z, t.x, t.y, t.z, 0.0f, 0.5f)   // left
    };

    meshData.Indices32 = { 0, 2, 1, 0, 3, 2 };
    return meshData;
}