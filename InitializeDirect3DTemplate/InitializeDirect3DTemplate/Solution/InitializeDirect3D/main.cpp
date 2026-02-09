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

    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
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

    // Generate data from the functions in GeometryGenerator.cpp 
    GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 1.0f);
    GeometryGenerator::MeshData prism = geoGen.CreateTriangularPrism(1.0f, 1.0f, 1.0f);
    GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f);

    // This section combines vertices into a single buffer for the GPU 
    // (Actual buffer creation omitted for brevity - uses d3dUtil::CreateDefaultBuffer)
}

void CastleApp::BuildRenderItems() {
    // TOWER 1 - NW
    auto towerNW = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&towerNW->World, XMMatrixTranslation(-10.0f, 0.0f, 10.0f));
    towerNW->IndexCount = 18; // Cylinder index count
    mAllRitems.push_back(std::move(towerNW));

    // ROOF 1 - NW
    auto roofNW = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&roofNW->World, XMMatrixTranslation(-10.0f, 5.0f, 10.0f));
    roofNW->IndexCount = 18; // Pyramid index count
    mAllRitems.push_back(std::move(roofNW));

    // WALL 1 - NORTH
    auto wallN = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wallN->World, XMMatrixScaling(20, 5, 1) * XMMatrixTranslation(0, 0, 10));
    wallN->IndexCount = 36; // Box index count
    mAllRitems.push_back(std::move(wallN));
}

void CastleApp::Update(const GameTimer& gt) {}

void CastleApp::Draw(const GameTimer& gt) {
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Set Viewport and Scissor
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Clear Render Target [cite: 4]
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    // Loop through every item and draw 
    for (size_t i = 0; i < mAllRitems.size(); ++i) {
        auto ri = mAllRitems[i].get();
        // Bind buffers and call DrawIndexedInstanced here
    }

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
    FlushCommandQueue();
}

void CastleApp::OnResize() { D3DApp::OnResize(); }