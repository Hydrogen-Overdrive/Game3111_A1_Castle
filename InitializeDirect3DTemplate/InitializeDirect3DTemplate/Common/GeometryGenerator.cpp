#include "GeometryGenerator.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

// --- NEW PRIMITIVES FOR ASSIGNMENT 1 --- [cite: 18, 27]

GeometryGenerator::MeshData GeometryGenerator::CreatePyramid(float width, float height, float depth)
{
    MeshData meshData;

    float w2 = width * 0.5f;
    float d2 = depth * 0.5f;

    // We define 5 points: 4 for the square base and 1 for the tip (apex)
    meshData.Vertices =
    {
        // Base vertices (at y = 0)
        Vertex(-w2, 0.0f, -d2, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f), // 0
        Vertex(w2, 0.0f, -d2, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f), // 1
        Vertex(w2, 0.0f,  d2, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f), // 2
        Vertex(-w2, 0.0f,  d2, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f), // 3
        // Apex vertex (at the top center)
        Vertex(0.0f, height, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f) // 4
    };

    // Connect the dots into triangles (Clockwise winding)
    meshData.Indices32 =
    {
        // Bottom Base (two triangles)
        0, 2, 1,
        0, 3, 2,
        // Four Sides
        0, 4, 1, // Front
        1, 4, 2, // Right
        2, 4, 3, // Back
        3, 4, 0  // Left
    };

    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateWedge(float width, float height, float depth)
{
    MeshData meshData;
    float w2 = width * 0.5f;
    float h2 = height * 0.5f;
    float d2 = depth * 0.5f;

    // A wedge is like a box but the top front edge is slanted down to the bottom front
    meshData.Vertices =
    {
        Vertex(-w2, -h2, -d2, 0,0,-1, 1,0,0, 0,1), // 0: Bottom-Front-Left
        Vertex(w2, -h2, -d2, 0,0,-1, 1,0,0, 1,1), // 1: Bottom-Front-Right
        Vertex(w2, -h2,  d2, 0,0, 1, 1,0,0, 1,0), // 2: Bottom-Back-Right
        Vertex(-w2, -h2,  d2, 0,0, 1, 1,0,0, 0,0), // 3: Bottom-Back-Left
        Vertex(-w2,  h2,  d2, 0,1, 0, 1,0,0, 0,1), // 4: Top-Back-Left
        Vertex(w2,  h2,  d2, 0,1, 0, 1,0,0, 1,1)  // 5: Top-Back-Right
    };

    meshData.Indices32 =
    {
        0, 3, 1, 1, 3, 2, // Bottom
        3, 4, 2, 2, 4, 5, // Back wall
        0, 4, 3,          // Left side
        1, 2, 5,          // Right side
        0, 5, 4, 0, 1, 5  // The Slant (Hypotenuse)
    };

    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateTriangularPrism(float width, float height, float depth)
{
    MeshData meshData;
    float w2 = width * 0.5f;
    float d2 = depth * 0.5f;

    // Two triangles (front and back) connected by rectangles
    meshData.Vertices =
    {
        Vertex(-w2, 0, -d2, 0,0,-1, 1,0,0, 0,1),    // 0
        Vertex(w2, 0, -d2, 0,0,-1, 1,0,0, 1,1),    // 1
        Vertex(0, height, -d2, 0,0,-1, 1,0,0, 0.5f,0), // 2 (Front peak)

        Vertex(-w2, 0, d2, 0,0,1, 1,0,0, 0,1),     // 3
        Vertex(w2, 0, d2, 0,0,1, 1,0,0, 1,1),     // 4
        Vertex(0, height, d2, 0,0,1, 1,0,0, 0.5f,0)   // 5 (Back peak)
    };

    meshData.Indices32 =
    {
        0, 2, 1,          // Front Face
        3, 4, 5,          // Back Face
        0, 3, 2, 2, 3, 5, // Left Slope
        1, 5, 4, 1, 2, 5, // Right Slope
        0, 1, 3, 1, 4, 3  // Bottom
    };

    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateDiamond(float width, float height)
{
    MeshData meshData;
    float w2 = width * 0.5f;
    float h2 = height * 0.5f;

    // Two pyramids joined at the base
    meshData.Vertices =
    {
        Vertex(0, h2, 0, 0,1,0, 1,0,0, 0.5f, 0),    // Top tip
        Vertex(-w2, 0, -w2, 0,0,-1, 1,0,0, 0, 0.5f), // Middle ring
        Vertex(w2, 0, -w2, 0,0,-1, 1,0,0, 1, 0.5f),
        Vertex(w2, 0, w2, 0,0,1, 1,0,0, 1, 0.5f),
        Vertex(-w2, 0, w2, 0,0,1, 1,0,0, 0, 0.5f),
        Vertex(0, -h2, 0, 0,-1,0, 1,0,0, 0.5f, 1)   // Bottom tip
    };

    meshData.Indices32 =
    {
        0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 1, 4, // Top half
        5, 1, 2, 5, 2, 3, 5, 3, 4, 5, 4, 1  // Bottom half
    };

    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::CreateCone(float radius, float height, uint32 sliceCount)
{
    MeshData meshData;

    // Tip of the cone
    meshData.Vertices.push_back(Vertex(0, height, 0, 0, 1, 0, 1, 0, 0, 0.5f, 0));

    // Base circle vertices
    float thetaStep = XM_2PI / sliceCount;
    for (uint32 i = 0; i <= sliceCount; ++i)
    {
        float theta = i * thetaStep;
        float x = radius * cosf(theta);
        float z = radius * sinf(theta);
        meshData.Vertices.push_back(Vertex(x, 0, z, 0, -1, 0, 1, 0, 0, 0, 1));
    }

    // Indices for the side of the cone
    for (uint32 i = 1; i <= sliceCount; ++i)
    {
        meshData.Indices32.push_back(0);
        meshData.Indices32.push_back(i + 1);
        meshData.Indices32.push_back(i);
    }

    return meshData;
}

// Simple Torus (Donut shape)
GeometryGenerator::MeshData GeometryGenerator::CreateTorus(float innerRadius, float outerRadius, uint32 sliceCount, uint32 stackCount)
{
    MeshData meshData;

    for (uint32 i = 0; i <= stackCount; ++i)
    {
        float phi = i * XM_2PI / stackCount;
        for (uint32 j = 0; j <= sliceCount; ++j)
        {
            float theta = j * XM_2PI / sliceCount;

            // Math for a torus point
            float x = (outerRadius + innerRadius * cosf(theta)) * cosf(phi);
            float y = innerRadius * sinf(theta);
            float z = (outerRadius + innerRadius * cosf(theta)) * sinf(phi);

            meshData.Vertices.push_back(Vertex(x, y, z, 0, 1, 0, 1, 0, 0, 0, 0));
        }
    }

    // Connect rings with triangles
    uint32 verticesPerRing = sliceCount + 1;
    for (uint32 i = 0; i < stackCount; ++i)
    {
        for (uint32 j = 0; j < sliceCount; ++j)
        {
            meshData.Indices32.push_back(i * verticesPerRing + j);
            meshData.Indices32.push_back((i + 1) * verticesPerRing + j);
            meshData.Indices32.push_back(i * verticesPerRing + j + 1);

            meshData.Indices32.push_back(i * verticesPerRing + j + 1);
            meshData.Indices32.push_back((i + 1) * verticesPerRing + j);
            meshData.Indices32.push_back((i + 1) * verticesPerRing + j + 1);
        }
    }

    return meshData;
}