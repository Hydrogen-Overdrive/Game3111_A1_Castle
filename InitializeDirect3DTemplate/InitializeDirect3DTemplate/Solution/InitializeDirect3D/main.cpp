#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"

using namespace DirectX;

// Structure to track individual castle pieces in the world
struct RenderItem {
    XMFLOAT4X4 World = MathHelper::Identity4x4();
    MeshGeometry* Geo = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

class CastleApp : public D3DApp {
public:
    CastleApp(HINSTANCE hInstance);
    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    void BuildGeometry();
    void BuildRenderItems();

    // Data members
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;

    // View matrix for camera [cite: 56]
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd) {
    try {
        CastleApp theApp(hInstance);
        if (!theApp.Initialize()) return 0;
        return theApp.Run();
    }
    catch (DxException& e) {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

CastleApp::CastleApp(HINSTANCE hInstance) : D3DApp(hInstance) {}

bool CastleApp::Initialize() {
    if (!D3DApp::Initialize()) return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    BuildGeometry();
    BuildRenderItems();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    FlushCommandQueue();

    return true;
}

void CastleApp::BuildGeometry() {
    GeometryGenerator geoGen;

    // 1. Generate mesh data [cite: 20, 27]
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.5f, 1.0f, 20, 20);
    GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 1.0f);
    GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f);

    // 2. Combine all vertices
    std::vector<GeometryGenerator::Vertex> vertices;
    vertices.insert(vertices.end(), box.Vertices.begin(), box.Vertices.end());
    vertices.insert(vertices.end(), cylinder.Vertices.begin(), cylinder.Vertices.end());
    vertices.insert(vertices.end(), pyramid.Vertices.begin(), pyramid.Vertices.end());
    vertices.insert(vertices.end(), wedge.Vertices.begin(), wedge.Vertices.end());

    // 3. Combine all indices
    std::vector<std::uint16_t> indices;
    for (auto& i : box.Indices32) indices.push_back((uint16_t)i);
    for (auto& i : cylinder.Indices32) indices.push_back((uint16_t)i);
    for (auto& i : pyramid.Indices32) indices.push_back((uint16_t)i);
    for (auto& i : wedge.Indices32) indices.push_back((uint16_t)i);

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(GeometryGenerator::Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "castleGeo";

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    // Set offsets for individual shapes
    geo->DrawArgs["box"] = { (UINT)box.Indices32.size(), 0, 0 };
    geo->DrawArgs["cylinder"] = { (UINT)cylinder.Indices32.size(), (UINT)box.Indices32.size(), static_cast<int>(box.Vertices.size()) };
    geo->DrawArgs["pyramid"] = { (UINT)pyramid.Indices32.size(), (UINT)(box.Indices32.size() + cylinder.Indices32.size()), static_cast<int>(box.Vertices.size() + cylinder.Vertices.size()) };
    geo->DrawArgs["wedge"] = { (UINT)wedge.Indices32.size(), (UINT)(box.Indices32.size() + cylinder.Indices32.size() + pyramid.Indices32.size()), static_cast<int>(box.Vertices.size() + cylinder.Vertices.size() + pyramid.Vertices.size()) };

    mGeometries[geo->Name] = std::move(geo);
}

void CastleApp::BuildRenderItems() {
    // Castle Design: 4 Towers at corners [cite: 6]
    float towerPos[4][2] = { {-10.0f, 10.0f}, {10.0f, 10.0f}, {-10.0f, -10.0f}, {10.0f, -10.0f} };

    for (int i = 0; i < 4; ++i) {
        // Tower Cylinders
        auto tower = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&tower->World, XMMatrixScaling(2, 8, 2) * XMMatrixTranslation(towerPos[i][0], 4, towerPos[i][1]));
        tower->Geo = mGeometries["castleGeo"].get();
        tower->IndexCount = tower->Geo->DrawArgs["cylinder"].IndexCount;
        tower->StartIndexLocation = tower->Geo->DrawArgs["cylinder"].StartIndexLocation;
        tower->BaseVertexLocation = tower->Geo->DrawArgs["cylinder"].BaseVertexLocation;
        mAllRitems.push_back(std::move(tower));

        // Tower Pyramid Roofs
        auto roof = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&roof->World, XMMatrixScaling(3, 3, 3) * XMMatrixTranslation(towerPos[i][0], 9.5f, towerPos[i][1]));
        roof->Geo = mGeometries["castleGeo"].get();
        roof->IndexCount = roof->Geo->DrawArgs["pyramid"].IndexCount;
        roof->StartIndexLocation = roof->Geo->DrawArgs["pyramid"].StartIndexLocation;
        roof->BaseVertexLocation = roof->Geo->DrawArgs["pyramid"].BaseVertexLocation;
        mAllRitems.push_back(std::move(roof));
    }

    // Walls connecting the towers [cite: 6]
    auto wallN = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wallN->World, XMMatrixScaling(20, 5, 1) * XMMatrixTranslation(0, 2.5, 10));
    wallN->Geo = mGeometries["castleGeo"].get();
    wallN->IndexCount = wallN->Geo->DrawArgs["box"].IndexCount;
    mAllRitems.push_back(std::move(wallN));

    // Entrance Ramp [cite: 7]
    auto ramp = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&ramp->World, XMMatrixScaling(4, 1, 6) * XMMatrixTranslation(0, 0, -13));
    ramp->Geo = mGeometries["castleGeo"].get();
    ramp->IndexCount = ramp->Geo->DrawArgs["wedge"].IndexCount;
    ramp->StartIndexLocation = ramp->Geo->DrawArgs["wedge"].StartIndexLocation;
    ramp->BaseVertexLocation = ramp->Geo->DrawArgs["wedge"].BaseVertexLocation;
    mAllRitems.push_back(std::move(ramp));
}

void CastleApp::Update(const GameTimer& gt) {
    // Camera placement looking down at the castle [cite: 49, 50]
    float x = 50.0f * sinf(0.25f * MathHelper::Pi) * cosf(1.5f * MathHelper::Pi);
    float z = 50.0f * sinf(0.25f * MathHelper::Pi) * sinf(1.5f * MathHelper::Pi);
    float y = 35.0f;

    XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);
}

void CastleApp::Draw(const GameTimer& gt) {
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    for (size_t i = 0; i < mAllRitems.size(); ++i) {
        auto ri = mAllRitems[i].get();
        mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        mCommandList->IASetPrimitiveTopology(ri->PrimitiveType);
        mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
    FlushCommandQueue();
}

void CastleApp::OnResize() { D3DApp::OnResize(); }