/** @file Week4-6-ShapeComplete.cpp
 * @brief Full Enclosed Castle Assignment - Final Submission.
 */

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct RenderItem
{
    RenderItem() = default;
    XMFLOAT4X4 World = MathHelper::Identity4x4();
    int NumFramesDirty = gNumFrameResources;
    UINT ObjCBIndex = -1;
    MeshGeometry* Geo = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

class ShapesApp : public D3DApp
{
public:
    ShapesApp(HINSTANCE hInstance);
    ShapesApp(const ShapesApp& rhs) = delete;
    ShapesApp& operator=(const ShapesApp& rhs) = delete;
    ~ShapesApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
    void UpdateCamera(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);

    void BuildDescriptorHeaps();
    void BuildConstantBufferViews();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::vector<RenderItem*> mOpaqueRitems;

    PassConstants mMainPassCB;
    UINT mPassCbvOffset = 0;
    bool mIsWireframe = false;

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f * XM_PI;
    float mPhi = 0.2f * XM_PI;
    float mRadius = 75.0f; // Zoomed out for full castle view

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    try {
        ShapesApp theApp(hInstance);
        if (!theApp.Initialize()) return 0;
        return theApp.Run();
    }
    catch (DxException& e) {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

ShapesApp::ShapesApp(HINSTANCE hInstance) : D3DApp(hInstance) {}
ShapesApp::~ShapesApp() { if (md3dDevice != nullptr) FlushCommandQueue(); }

bool ShapesApp::Initialize()
{
    if (!D3DApp::Initialize()) return false;
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildRenderItems();
    BuildFrameResources();
    BuildDescriptorHeaps();
    BuildConstantBufferViews();
    BuildPSOs();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    FlushCommandQueue();
    return true;
}

void ShapesApp::OnResize()
{
    D3DApp::OnResize();
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
    UpdateCamera(gt);
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());

    if (mIsWireframe) {
        ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
    }
    else {
        ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
    }

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
    auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
    mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

    DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y) { mLastMousePos.x = x; mLastMousePos.y = y; SetCapture(mhMainWnd); }
void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y) { ReleaseCapture(); }
void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y) {
    if ((btnState & MK_LBUTTON) != 0) {
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));
        mTheta += dx; mPhi += dy;
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0) {
        float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);
        mRadius += dx - dy;
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }
    mLastMousePos.x = x; mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt) { mIsWireframe = (GetAsyncKeyState('1') & 0x8000); }

void ShapesApp::UpdateCamera(const GameTimer& gt) {
    mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
    mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
    mEyePos.y = mRadius * cosf(mPhi);
    XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt) {
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for (auto& e : mAllRitems) {
        if (e->NumFramesDirty > 0) {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            currObjectCB->CopyData(e->ObjCBIndex, objConstants);
            e->NumFramesDirty--;
        }
    }
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt) {
    XMMATRIX view = XMLoadFloat4x4(&mView);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    mMainPassCB.EyePosW = mEyePos;
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 1.0f; mMainPassCB.FarZ = 1000.0f;
    mMainPassCB.TotalTime = gt.TotalTime(); mMainPassCB.DeltaTime = gt.DeltaTime();
    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::BuildDescriptorHeaps() {
    UINT objCount = (UINT)mOpaqueRitems.size();
    UINT numDescriptors = (objCount + 1) * gNumFrameResources;
    mPassCbvOffset = objCount * gNumFrameResources;
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = numDescriptors;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));
}

void ShapesApp::BuildConstantBufferViews() {
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT objCount = (UINT)mOpaqueRitems.size();
    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
        auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
        for (UINT i = 0; i < objCount; ++i) {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();
            cbAddress += i * objCBByteSize;
            int heapIndex = frameIndex * objCount + i;
            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
            handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
            cbvDesc.BufferLocation = cbAddress;
            cbvDesc.SizeInBytes = objCBByteSize;
            md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
        }
    }
    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
        auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
        D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();
        int heapIndex = mPassCbvOffset + frameIndex;
        auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
        cbvDesc.BufferLocation = cbAddress;
        cbvDesc.SizeInBytes = passCBByteSize;
        md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
    }
}

void ShapesApp::BuildRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE cbvTable0, cbvTable1;
    cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
    slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> serializedRootSig = nullptr, errorBlob = nullptr;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(md3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShadersAndInputLayout() {
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_1");
    mInputLayout = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };
}

void ShapesApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(30.0f, 30.0f, 60, 40);
    GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
    GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.5f, 3.0f, 20, 20);
    GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 1.0f);

    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
    UINT pyramidVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
    UINT pyramidIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

    SubmeshGeometry boxSubmesh; boxSubmesh.IndexCount = (UINT)box.Indices32.size(); boxSubmesh.StartIndexLocation = boxIndexOffset; boxSubmesh.BaseVertexLocation = boxVertexOffset;
    SubmeshGeometry gridSubmesh; gridSubmesh.IndexCount = (UINT)grid.Indices32.size(); gridSubmesh.StartIndexLocation = gridIndexOffset; gridSubmesh.BaseVertexLocation = gridVertexOffset;
    SubmeshGeometry sphereSubmesh; sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size(); sphereSubmesh.StartIndexLocation = sphereIndexOffset; sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
    SubmeshGeometry cylinderSubmesh; cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size(); cylinderSubmesh.StartIndexLocation = cylinderIndexOffset; cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;
    SubmeshGeometry pyramidSubmesh; pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size(); pyramidSubmesh.StartIndexLocation = pyramidIndexOffset; pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

    auto totalVertexCount = box.Vertices.size() + grid.Vertices.size() + sphere.Vertices.size() + cylinder.Vertices.size() + pyramid.Vertices.size();
    std::vector<Vertex> vertices(totalVertexCount);
    UINT k = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k) { vertices[k].Pos = box.Vertices[i].Position; vertices[k].Color = XMFLOAT4(DirectX::Colors::Gold); }
    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) { vertices[k].Pos = grid.Vertices[i].Position; vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen); }
    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) { vertices[k].Pos = sphere.Vertices[i].Position; vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson); }
    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) { vertices[k].Pos = cylinder.Vertices[i].Position; vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue); }
    for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k) { vertices[k].Pos = pyramid.Vertices[i].Position; vertices[k].Color = XMFLOAT4(DirectX::Colors::Red); }

    std::vector<std::uint16_t> indices;
    for (auto i : box.Indices32) indices.push_back((uint16_t)i);
    for (auto i : grid.Indices32) indices.push_back((uint16_t)i);
    for (auto i : sphere.Indices32) indices.push_back((uint16_t)i);
    for (auto i : cylinder.Indices32) indices.push_back((uint16_t)i);
    for (auto i : pyramid.Indices32) indices.push_back((uint16_t)i);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";
    ThrowIfFailed(D3DCreateBlob(vertices.size() * sizeof(Vertex), &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertices.size() * sizeof(Vertex));
    ThrowIfFailed(D3DCreateBlob(indices.size() * sizeof(std::uint16_t), &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), indices.size() * sizeof(std::uint16_t));
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vertices.data(), vertices.size() * sizeof(Vertex), geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), indices.data(), indices.size() * sizeof(std::uint16_t), geo->IndexBufferUploader);
    geo->VertexByteStride = sizeof(Vertex); geo->VertexBufferByteSize = vertices.size() * sizeof(Vertex); geo->IndexFormat = DXGI_FORMAT_R16_UINT; geo->IndexBufferByteSize = indices.size() * sizeof(std::uint16_t);
    geo->DrawArgs["box"] = boxSubmesh; geo->DrawArgs["grid"] = gridSubmesh; geo->DrawArgs["sphere"] = sphereSubmesh; geo->DrawArgs["cylinder"] = cylinderSubmesh;
    geo->DrawArgs["pyramid"] = pyramidSubmesh;
    mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildPSOs() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    opaquePsoDesc.pRootSignature = mRootSignature.Get();
    opaquePsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), mShaders["standardVS"]->GetBufferSize() };
    opaquePsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()), mShaders["opaquePS"]->GetBufferSize() };
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
    opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}

void ShapesApp::BuildFrameResources() {
    for (int i = 0; i < gNumFrameResources; ++i)
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, 100)); // Increased limit
}

void ShapesApp::BuildRenderItems()
{
    UINT objCBIndex = 0;

    // 1. FLOOR
    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    gridRitem->ObjCBIndex = objCBIndex++;
    gridRitem->Geo = mGeometries["shapeGeo"].get();
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    mAllRitems.push_back(std::move(gridRitem));

    // 2. CORNER TOWERS AND ROOFS
    float towerPos[4][2] = { {-12, 12}, {12, 12}, {-12, -12}, {12, -12} };
    for (int i = 0; i < 4; ++i) {
        auto tower = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&tower->World, XMMatrixScaling(2.5, 5.5, 2.5) * XMMatrixTranslation(towerPos[i][0], 8.25f, towerPos[i][1]));
        tower->ObjCBIndex = objCBIndex++;
        tower->Geo = mGeometries["shapeGeo"].get();
        tower->IndexCount = tower->Geo->DrawArgs["cylinder"].IndexCount;
        tower->StartIndexLocation = tower->Geo->DrawArgs["cylinder"].StartIndexLocation;
        tower->BaseVertexLocation = tower->Geo->DrawArgs["cylinder"].BaseVertexLocation;
        mAllRitems.push_back(std::move(tower));

        auto roof = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&roof->World, XMMatrixScaling(4.2, 4.2, 4.2) * XMMatrixTranslation(towerPos[i][0], 17.5f, towerPos[i][1]));
        roof->ObjCBIndex = objCBIndex++;
        roof->Geo = mGeometries["shapeGeo"].get();
        roof->IndexCount = roof->Geo->DrawArgs["pyramid"].IndexCount;
        roof->StartIndexLocation = roof->Geo->DrawArgs["pyramid"].StartIndexLocation;
        roof->BaseVertexLocation = roof->Geo->DrawArgs["pyramid"].BaseVertexLocation;
        mAllRitems.push_back(std::move(roof));
    }

    // 3. ENCLOSING WALLS
    // Back Wall (North)
    auto wallN = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wallN->World, XMMatrixScaling(24, 8, 1) * XMMatrixTranslation(0, 4, 12));
    wallN->ObjCBIndex = objCBIndex++;
    wallN->Geo = mGeometries["shapeGeo"].get();
    wallN->IndexCount = wallN->Geo->DrawArgs["box"].IndexCount;
    wallN->StartIndexLocation = wallN->Geo->DrawArgs["box"].StartIndexLocation;
    wallN->BaseVertexLocation = wallN->Geo->DrawArgs["box"].BaseVertexLocation;
    mAllRitems.push_back(std::move(wallN));

    // Side Walls (East & West)
    float sideX[2] = { -12, 12 };
    for (int i = 0; i < 2; ++i) {
        auto sideWall = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&sideWall->World, XMMatrixScaling(1, 8, 24) * XMMatrixTranslation(sideX[i], 4, 0));
        sideWall->ObjCBIndex = objCBIndex++;
        sideWall->Geo = mGeometries["shapeGeo"].get();
        sideWall->IndexCount = sideWall->Geo->DrawArgs["box"].IndexCount;
        sideWall->StartIndexLocation = sideWall->Geo->DrawArgs["box"].StartIndexLocation;
        sideWall->BaseVertexLocation = sideWall->Geo->DrawArgs["box"].BaseVertexLocation;
        mAllRitems.push_back(std::move(sideWall));
    }

    // Front Gatehouse Walls (Two walls with entrance gap)
    float gateWallX[2] = { -8.5, 8.5 };
    for (int i = 0; i < 2; ++i) {
        auto gWall = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&gWall->World, XMMatrixScaling(7, 8, 1) * XMMatrixTranslation(gateWallX[i], 4, -12));
        gWall->ObjCBIndex = objCBIndex++;
        gWall->Geo = mGeometries["shapeGeo"].get();
        gWall->IndexCount = gWall->Geo->DrawArgs["box"].IndexCount;
        gWall->StartIndexLocation = gWall->Geo->DrawArgs["box"].StartIndexLocation;
        gWall->BaseVertexLocation = gWall->Geo->DrawArgs["box"].BaseVertexLocation;
        mAllRitems.push_back(std::move(gWall));
    }

    // 4. THE DRAWBRIDGE
    auto bridge = std::make_unique<RenderItem>();
    // Scale flat and long, rotate down, and place at entrance gap
    XMMATRIX bridgeWorld = XMMatrixScaling(10, 0.4f, 13) * XMMatrixRotationX(XMConvertToRadians(-18.0f)) * XMMatrixTranslation(0, 0.4f, -18.5f);
    XMStoreFloat4x4(&bridge->World, bridgeWorld);
    bridge->ObjCBIndex = objCBIndex++;
    bridge->Geo = mGeometries["shapeGeo"].get();
    bridge->IndexCount = bridge->Geo->DrawArgs["box"].IndexCount;
    bridge->StartIndexLocation = bridge->Geo->DrawArgs["box"].StartIndexLocation;
    bridge->BaseVertexLocation = bridge->Geo->DrawArgs["box"].BaseVertexLocation;
    mAllRitems.push_back(std::move(bridge));

    for (auto& e : mAllRitems) mOpaqueRitems.push_back(e.get());
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    for (size_t i = 0; i < ritems.size(); ++i) {
        auto ri = ritems[i];
        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);
        UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;
        auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
        cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);
        cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);
        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}