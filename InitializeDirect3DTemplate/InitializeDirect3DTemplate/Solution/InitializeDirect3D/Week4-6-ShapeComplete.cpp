/** @file Week4-6-ShapeComplete.cpp
 *  Castle scene: textures, Blinn-Phong lighting, alpha-blended water, billboard trees (geometry shader), maze walkthrough.
 *
 *  Recent layout / gameplay notes (for readers new to the file):
 *  - Maze grid (MazeConfig::kLines): ASCII rows drive wall meshes and colliders; '#' is solid. The castle end
 *    includes wide centre openings for a drawbridge yard; the old single hedge row was removed so the yard
 *    opens toward the castle. Row indices increase toward the castle; the last row is still maze-driven.
 *  - Trees: two rings of billboard points (inner preserves the original 12 angles at radius 34; outer ring
 *    fills the island). Samples that fall inside the maze bounding box are pushed outward so trees stay off paths.
 *  - Walking: PlayerIntersectsMaze uses an axis-aligned box around the body. kCameraNearZ tightens the view
 *    frustum near plane (smaller than 1.0) so walls do not disappear when you stand close; kPlayerHalfXZ and
 *    kPlayerMoveSubsteps reduce clipping and fast-move tunneling through thin colliders.
 *  - Courtyard props (spikes, gallows) use yard Z derived from the last maze row when no separate hedge row exists.
 */

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/DDSTextureLoader.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Camera.h"
#include "SkullTxtMesh.h"

#include <cmath>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

// Billboard trees: two rings (inner = 12 trees at fixed radius; outer = 30 trees) for perimeter coverage.
static constexpr int kBillboardOriginalRingTrees = 12;
static constexpr int kBillboardOuterRingTrees = 30;
static constexpr int kBillboardTreeCount = kBillboardOriginalRingTrees + kBillboardOuterRingTrees;
static constexpr float kTreeBillboardHalfW = 4.5f;
static constexpr float kTreeBillboardHalfH = 4.75f; // world Y = this so billboard base meets ground

struct AxisAlignedCollider
{
    XMFLOAT3 Center;
    XMFLOAT3 HalfExtents;
};

namespace MazeConfig {
constexpr float kCell = 1.65f;
// U-shaped (3-sided) maze: long walls on west, south, east; open north toward the castle (back).
constexpr int kYardOpenRows = 0; // hedge row removed — open yard for gallows / drawbridge
constexpr int kRows = 26 + kYardOpenRows; // labyrinth only (= 26)
constexpr int kCols = 23;
constexpr float kOriginX = -0.5f * kCols * kCell;
// Maze sits in front of the castle; north edge opens to the bridge / gate (castle is the goal at the back).
// Wall half-depth in Z matches BuildRenderItems (kCell * 0.92f).
constexpr float kWallScaleZ = 0.92f;
// Shift last maze row in Z vs grid; 0 = aligned with maze grid (no pull toward moat).
constexpr float kLastRowZOffsetTowardCastle = 0.0f;
constexpr float kMazeNorthEdgeZ = -19.0f;
constexpr float kOriginZ =
    kMazeNorthEdgeZ - (static_cast<float>(kRows) - 0.5f + 0.5f * kWallScaleZ) * kCell;
constexpr float kWallHeight = 3.0f;
constexpr float kEyeHeight = 2.45f;
// Player collision box half-extents (XZ horizontal, Y vertical). Slightly wider XZ stops the camera before it hugs the wall mesh.
constexpr float kPlayerHalfXZ = 0.52f;
constexpr float kPlayerHalfY = 0.95f;
constexpr float kPlayerBodyCenterY = 1.0f;
// Perspective near plane (world units). Smaller values let the renderer draw geometry closer to the eye; 1.0f made walls vanish when near.
constexpr float kCameraNearZ = 0.08f;
// How many collision checks per frame along the move vector (avoids skipping past a thin wall in one big step).
constexpr int kPlayerMoveSubsteps = 8;

inline bool AabbOverlap(const XMFLOAT3& ca, const XMFLOAT3& ha, const XMFLOAT3& cb, const XMFLOAT3& hb)
{
    return fabsf(ca.x - cb.x) <= ha.x + hb.x
        && fabsf(ca.y - cb.y) <= ha.y + hb.y
        && fabsf(ca.z - cb.z) <= ha.z + hb.z;
}

// '#' = wall. Labyrinth, then open yard rows, then gate. Row 0 = entrance (wide center gap for drawbridge).
const char* const kLines[kRows] = {
    "########       ########",
    "#     #     #   #     #",
    "# # ### ##### # # # # #",
    "# # #   #     # # # # #",
    "# # # ### # ##### # # #",
    "# #   #   #     # # # #",
    "# ##### ####### # # # #",
    "# # #   #     #   # # #",
    "# # # ####### ##### # #",
    "# #   #       #     # #",
    "# ### # ##### # ##### #",
    "#   #   #     #   # # #",
    "### ##### ####### # # #",
    "# # # #     #     #   #",
    "# # # # ##### ##### ###",
    "# # #   #     #       #",
    "# # ##### ########### #",
    "# #     #           # #",
    "# ##### ####### ### # #",
    "# #     #     # #   # #",
    "# # ##### ### ### #####",
    "# # #   #   #     #   #",
    "# # # # # # ####### # #",
    // Drawbridge yard: outer boundary # only; wide centre opening (same on two rows).
    "#                     #",
    "#                     #",
    "# #             #     #",
};
} // namespace MazeConfig

// Matches Luna Week 7 (Crate): World + TexTransform per object; shader applies gTexTransform to UVs in color.hlsl VS.
struct RenderItem
{
    RenderItem() = default;
    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4(); // row-major; transpose when copying to ObjectConstants
    int NumFramesDirty = gNumFrameResources;
    UINT ObjCBIndex = -1;
    UINT TexSrvHeapIndex = 0; // texture SRV index in the shader-visible SRV heap
    float Alpha = 1.0f;          // < 1.0 draws with the transparent PSO (water, fountain); shader uses gAlpha
    float WaterSurfaceUv = 0.0f; // copied to GPU cbuffer for layout (must match ObjectConstants / tree shader)
    XMFLOAT3 BaseColorMul = { 1.0f, 1.0f, 1.0f }; // multiplies texture after sampling ( black moat floor)
    MeshGeometry* Geo = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    bool BillboardTree = false;
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
    void SetupMazeCamera();
    bool PlayerIntersectsMaze(const XMFLOAT3& eyeWorld) const;
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);

    void BuildDescriptorHeaps();
    void LoadTextures();
    void BuildTextureSRVs();
    void BuildConstantBufferViews();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildSkullTxtGeometry();
    void BuildTreeBillboardGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
    void DrawBillboardTrees(ID3D12GraphicsCommandList* cmdList);

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;
    //  We use a single shader-visible CBV/SRV/UAV heap for both:
    // - CBVs (object + pass)
    // - SRVs (textures)
    UINT mTextureSrvBaseIndex = 0;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    // Texture SRV slots for the shader-visible descriptor heap (t0).
    // We keep slot names aligned with the castle parts we render.
    static constexpr UINT kNumTextures = 13;
    enum TextureSlot : UINT
    {
        TexStone = 0,        // stone trunk
        TexStoneNeck = 1,    // different stone for tower neck
        TexStoneTop = 2,     // different stone for tower domes
        TexBrickBase = 3,    // wall body / base brick
        TexBrickTop = 4,     // battlement / wall top brick
        TexWood = 5,         // gate + wood props
        TexWoodBridge = 6,  // drawbridge wood variation
        TexGrass = 7,        // ground
        TexWater = 8,        // water
        TexTreeLeaves = 9,   // treearray.dds — billboard trees (treeBillboard.hlsl)
        // Week 12–style ritual props: copy your Week12 DDS into Textures with these names, or they alias existing files.
        TexW12Skull = 10,
        TexW12Bones = 11,
        TexW12Ritual = 12,
    };

    struct TextureData
    {
        ComPtr<ID3D12Resource> Resource = nullptr;
        ComPtr<ID3D12Resource> UploadHeap = nullptr;
    };

    TextureData mTextureData[kNumTextures];

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::vector<RenderItem*> mOpaqueRitems;
    std::vector<RenderItem*> mBillboardTreeRitems;
    std::vector<RenderItem*> mTransparentRitems;

    PassConstants mMainPassCB;
    UINT mPassCbvOffset = 0;
    bool mIsWireframe = false;

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    Camera mCamera;
    std::vector<AxisAlignedCollider> mMazeColliders;

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
    BuildSkullTxtGeometry();
    BuildTreeBillboardGeometry();
    BuildRenderItems();
    BuildFrameResources();
    BuildDescriptorHeaps();
    LoadTextures();
    BuildTextureSRVs();
    BuildConstantBufferViews();
    BuildPSOs();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    FlushCommandQueue();
    SetupMazeCamera();
    return true;
}

void ShapesApp::OnResize()
{
    D3DApp::OnResize();
    // Near/far must stay in sync with UpdateMainPassCB (shader uses the same near distance for lighting math).
    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), MazeConfig::kCameraNearZ, 1000.0f);
    XMMATRIX P = mCamera.GetProj();
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

    DrawBillboardTrees(mCommandList.Get());

    if (!mIsWireframe && !mTransparentRitems.empty()) {
        mCommandList->SetPipelineState(mPSOs["transparent"].Get());
        // Sort transparent objects back-to-front based on camera distance.
        // This prevents alpha blending artifacts where water/fountain draws on top
        // of closer objects.
        std::vector<RenderItem*> transparentSorted = mTransparentRitems;
        std::sort(transparentSorted.begin(), transparentSorted.end(),
            [&](RenderItem* a, RenderItem* b) {
                // Translation part of the world matrix holds object position.
                const XMFLOAT3 ap = { a->World._41, a->World._42, a->World._43 };
                const XMFLOAT3 bp = { b->World._41, b->World._42, b->World._43 };
                const float adx = mEyePos.x - ap.x;
                const float ady = mEyePos.y - ap.y;
                const float adz = mEyePos.z - ap.z;
                const float bdx = mEyePos.x - bp.x;
                const float bdy = mEyePos.y - bp.y;
                const float bdz = mEyePos.z - bp.z;
                const float da = adx * adx + ady * ady + adz * adz;
                const float db = bdx * bdx + bdy * bdy + bdz * bdz;
                return da > db; // far first
            });
        DrawRenderItems(mCommandList.Get(), transparentSorted);
    }

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}
void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    if ((btnState & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0)
        ReleaseCapture();
}
void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if (((btnState & MK_LBUTTON) != 0) || ((btnState & MK_RBUTTON) != 0)) {
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));
        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
        mCamera.UpdateViewMatrix();
    }
    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
    using namespace MazeConfig;
    mIsWireframe = (GetAsyncKeyState('1') & 0x8000);

    const float dt = gt.DeltaTime();
    const float walkSpeed = 10.0f;
    const float flySpeed = 22.0f;
    XMFLOAT3 pos = mCamera.GetPosition3f();

    const bool freeFly = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const float moveSpeed = freeFly ? flySpeed : walkSpeed;

    XMVECTOR forward = XMVectorZero();
    XMVECTOR right = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    if (freeFly) {
        forward = mCamera.GetLook();
        float fl = XMVectorGetX(XMVector3Length(forward));
        if (fl > 1e-4f)
            forward = XMVectorScale(forward, 1.0f / fl);
        right = mCamera.GetRight();
        float rl = XMVectorGetX(XMVector3Length(right));
        if (rl > 1e-4f)
            right = XMVectorScale(right, 1.0f / rl);
        up = mCamera.GetUp();
        float ul = XMVectorGetX(XMVector3Length(up));
        if (ul > 1e-4f)
            up = XMVectorScale(up, 1.0f / ul);
    }
    else {
        forward = mCamera.GetLook();
        forward = XMVectorSet(XMVectorGetX(forward), 0.0f, XMVectorGetZ(forward), 0.0f);
        float flen = XMVectorGetX(XMVector3Length(forward));
        if (flen > 1e-4f)
            forward = XMVectorScale(forward, 1.0f / flen);

        right = mCamera.GetRight();
        right = XMVectorSet(XMVectorGetX(right), 0.0f, XMVectorGetZ(right), 0.0f);
        float rlen = XMVectorGetX(XMVector3Length(right));
        if (rlen > 1e-4f)
            right = XMVectorScale(right, 1.0f / rlen);
    }

    XMVECTOR move = XMVectorZero();
    if (GetAsyncKeyState('W') & 0x8000)
        move = XMVectorAdd(move, XMVectorScale(forward, moveSpeed * dt));
    if (GetAsyncKeyState('S') & 0x8000)
        move = XMVectorAdd(move, XMVectorScale(forward, -moveSpeed * dt));
    if (GetAsyncKeyState('A') & 0x8000)
        move = XMVectorAdd(move, XMVectorScale(right, -moveSpeed * dt));
    if (GetAsyncKeyState('D') & 0x8000)
        move = XMVectorAdd(move, XMVectorScale(right, moveSpeed * dt));
    if (GetAsyncKeyState(VK_UP) & 0x8000)
        move = XMVectorAdd(move, XMVectorScale(forward, moveSpeed * dt));
    if (GetAsyncKeyState(VK_DOWN) & 0x8000)
        move = XMVectorAdd(move, XMVectorScale(forward, -moveSpeed * dt));
    if (GetAsyncKeyState(VK_LEFT) & 0x8000)
        move = XMVectorAdd(move, XMVectorScale(right, -moveSpeed * dt));
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
        move = XMVectorAdd(move, XMVectorScale(right, moveSpeed * dt));
    if (freeFly) {
        if ((GetAsyncKeyState(VK_SPACE) & 0x8000) || (GetAsyncKeyState('E') & 0x8000))
            move = XMVectorAdd(move, XMVectorScale(up, moveSpeed * dt));
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState('Q') & 0x8000))
            move = XMVectorAdd(move, XMVectorScale(up, -moveSpeed * dt));
    }

    XMFLOAT3 delta;
    XMStoreFloat3(&delta, move);

    if (freeFly) {
        pos.x += delta.x;
        pos.y += delta.y;
        pos.z += delta.z;
    }
    else {
        // Sub-step horizontal motion: same sliding test as before, but in smaller pieces so we do not miss a wall between frames.
        const float subX = delta.x / static_cast<float>(kPlayerMoveSubsteps);
        const float subZ = delta.z / static_cast<float>(kPlayerMoveSubsteps);
        for (int step = 0; step < kPlayerMoveSubsteps; ++step) {
            const float nx = pos.x + subX;
            const float nz = pos.z + subZ;

            const XMFLOAT3 tryFull(nx, pos.y, nz);
            if (!PlayerIntersectsMaze(tryFull)) {
                pos.x = nx;
                pos.z = nz;
            }
            else {
                const XMFLOAT3 tryX(nx, pos.y, pos.z);
                if (!PlayerIntersectsMaze(tryX))
                    pos.x = nx;
                const XMFLOAT3 tryZ(pos.x, pos.y, nz);
                if (!PlayerIntersectsMaze(tryZ))
                    pos.z = nz;
            }
        }
    }

    mCamera.SetPosition(pos.x, pos.y, pos.z);
    mCamera.UpdateViewMatrix();
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
    mCamera.UpdateViewMatrix();
    mEyePos = mCamera.GetPosition3f();
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();
    XMStoreFloat4x4(&mView, view);
    XMStoreFloat4x4(&mProj, proj);
}

void ShapesApp::SetupMazeCamera()
{
    using namespace MazeConfig;
    int entranceCol = kCols / 2;
    for (int c = 0; c < kCols; ++c) {
        if (kLines[0][c] == ' ') {
            entranceCol = c;
            break;
        }
    }
    const float cellCenterX = kOriginX + (entranceCol + 0.5f) * kCell;
    const float southEdgeZ = kOriginZ + 0.5f * kCell;
    const float camZ = kOriginZ - 1.35f * kCell;
    const XMFLOAT3 eyePos(cellCenterX, kEyeHeight, camZ);
    const XMFLOAT3 target(cellCenterX, kEyeHeight * 0.55f, southEdgeZ + kCell * 0.75f);
    mCamera.LookAt(eyePos, target, XMFLOAT3(0.0f, 1.0f, 0.0f));
    mCamera.UpdateViewMatrix();
}

bool ShapesApp::PlayerIntersectsMaze(const XMFLOAT3& eyeWorld) const
{
    using namespace MazeConfig;
    const XMFLOAT3 pCen(eyeWorld.x, kPlayerBodyCenterY, eyeWorld.z);
    // XZ footprint must use kPlayerHalfXZ on both horizontal axes (was wrongly kPlayerHalfY on Z).
    const XMFLOAT3 pHalf(kPlayerHalfXZ, kPlayerHalfY, kPlayerHalfXZ);
    for (const auto& w : mMazeColliders) {
        if (AabbOverlap(pCen, pHalf, w.Center, w.HalfExtents))
            return true;
    }
    return false;
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
    // Luna Week 7: upload World and TexTransform (row-major in C++; transpose for HLSL column-major cbuffers).
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for (auto& e : mAllRitems)
    {
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants{};
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
            objConstants.Alpha = e->Alpha;
            objConstants.BaseColorMul = e->BaseColorMul;
            objConstants.WaterSurfaceUv = e->WaterSurfaceUv;

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
    mMainPassCB.NearZ = mCamera.GetNearZ();
    mMainPassCB.FarZ = mCamera.GetFarZ();
    mMainPassCB.TotalTime = gt.TotalTime(); mMainPassCB.DeltaTime = gt.DeltaTime();

    // Pass constants feed color.hlsl and treeBillboard.hlsl (gAmbientLight, gLights, gTotalTime).
    mMainPassCB.AmbientLight = XMFLOAT4(0.25f, 0.25f, 0.25f, 1.0f);

    
    for (int i = 0; i < MaxLights; ++i) {
        mMainPassCB.Lights[i].Strength = XMFLOAT3(0.0f, 0.0f, 0.0f);
        mMainPassCB.Lights[i].FalloffStart = 0.0f;
        mMainPassCB.Lights[i].Direction = XMFLOAT3(0.0f, 0.0f, 0.0f);
        mMainPassCB.Lights[i].FalloffEnd = 0.0f;
        mMainPassCB.Lights[i].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
        mMainPassCB.Lights[i].SpotPower = 0.0f;
    }

    // Lights[0]: directional — Direction is toward the scene (here, straight down). Strength is RGB color/intensity.
    mMainPassCB.Lights[0].Strength = XMFLOAT3(1.0f, 0.92f, 0.78f);
    mMainPassCB.Lights[0].Direction = XMFLOAT3(0.0f, -1.0f, 0.0f);

    // Lights[1..3]: point lights on a horizontal ring at y = 10 (warm accent). FalloffStart/End control reach.
    const float ringRadius = 30.0f;
    const float y = 10.0f;
    for (int pi = 0; pi < 3; ++pi) {
        float a = pi * (XM_2PI / 3.0f);
        float x = ringRadius * cosf(a);
        float z = ringRadius * sinf(a);

        auto& L = mMainPassCB.Lights[1 + pi];
        L.Strength = XMFLOAT3(0.95f, 0.28f, 0.12f);
        L.Position = XMFLOAT3(x, y, z);
        L.FalloffStart = 5.0f;
        L.FalloffEnd = 120.0f;
    }

    // Lights[4]: point light over the inner courtyard (center of castle yard). Tweak Y and Strength for your look.
    {
        auto& L = mMainPassCB.Lights[4];
        L.Strength = XMFLOAT3(0.55f, 0.48f, 0.35f);
        L.Position = XMFLOAT3(0.0f, 12.0f, 0.0f);
        L.FalloffStart = 4.0f;
        L.FalloffEnd = 55.0f;
    }

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::BuildDescriptorHeaps() {
    UINT objCount = (UINT)mAllRitems.size();
    UINT numDescriptors = (objCount + 1) * gNumFrameResources + kNumTextures;
    mPassCbvOffset = objCount * gNumFrameResources;
    mTextureSrvBaseIndex = (objCount + 1) * gNumFrameResources;
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = numDescriptors;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));
}

static std::wstring GetTextureAbsPath(const wchar_t* filename, const wchar_t* texturesFolderRelativeToExe = L"..\\..\\..\\Textures") {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring exeStr = exePath;
    size_t pos = exeStr.find_last_of(L"\\/");
    std::wstring exeDir = (pos == std::wstring::npos) ? L"" : exeStr.substr(0, pos + 1);

    // Try multiple relative locations 
    // output folders (x64\\Debug, x64\\DebugFoo, etc.). This keeps texture loading stable.
    const wchar_t* candidates[] = {
        texturesFolderRelativeToExe, // default
        L"..\\..\\..\\..\\Textures",
        L"..\\..\\..\\..\\..\\Textures",
        L"..\\..\\..\\..\\..\\..\\Textures",
    };

    for (const wchar_t* rel : candidates) {
        std::wstring abs = exeDir + rel + L"\\" + filename;
        if (GetFileAttributesW(abs.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return abs;
        }
    }

    // Fallback to the default (so error messages still show the expected path).
    return exeDir + texturesFolderRelativeToExe + L"\\" + filename;
}

void ShapesApp::LoadTextures() {
    // Each entry must match TextureSlot order; RenderItem::TexSrvHeapIndex picks which DDS is sampled as gDiffuseMap.
    struct TexEntry { UINT slot; const wchar_t* file; };
    const TexEntry entries[] = {
        { TexStone, L"stone.dds" },
        { TexStoneNeck, L"tile.dds" },
        // Tower tops: crossbones skull texture.
        { TexStoneTop, L"skull_crossbones_DXT5.dds" },
        // Use different brick looks for walls vs battlements.
        { TexBrickBase, L"bricks2.dds" },
        { TexBrickTop, L"bricks.dds" },
        { TexWood, L"WoodCrate01.dds" },
        { TexWoodBridge, L"WoodCrate02.dds" },
        { TexGrass, L"grass.dds" },
        { TexWater, L"water1.dds" },
        { TexTreeLeaves, L"treearray.dds" },
        // Week 12 ritual pack: drop your DDS into Textures as these names, or they alias existing art until then.
        { TexW12Skull, L"skull_crossbones_DXT5.dds" },
        { TexW12Bones, L"skull_crossbones_DXT5.dds" },
        { TexW12Ritual, L"bricks2.dds" },
    };

    for (const auto& e : entries) {
        std::wstring absPath = GetTextureAbsPath(e.file);
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
            md3dDevice.Get(), mCommandList.Get(), absPath.c_str(),
            mTextureData[e.slot].Resource,
            mTextureData[e.slot].UploadHeap));
    }
}

void ShapesApp::BuildTextureSRVs() {
    CD3DX12_CPU_DESCRIPTOR_HANDLE texHandle(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
    texHandle.Offset(mTextureSrvBaseIndex, mCbvSrvUavDescriptorSize);

    for (UINT i = 0; i < kNumTextures; ++i) {
        auto& tex = mTextureData[i];
        if (tex.Resource == nullptr)
            continue;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        auto desc = tex.Resource->GetDesc();
        srvDesc.Format = desc.Format;

        // Some DDS files might be arrays or cubemaps; create an SRV that matches.
        if (desc.DepthOrArraySize == 6 && desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.MipLevels = desc.MipLevels;
            srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        }
        else if (desc.DepthOrArraySize > 1 && desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            srvDesc.Texture2DArray.MostDetailedMip = 0;
            srvDesc.Texture2DArray.MipLevels = desc.MipLevels;
            srvDesc.Texture2DArray.FirstArraySlice = 0;
            srvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
            srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
        }
        else {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = desc.MipLevels;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        }

        md3dDevice->CreateShaderResourceView(tex.Resource.Get(), &srvDesc, texHandle);
        texHandle.Offset(1, mCbvSrvUavDescriptorSize);
    }
}

void ShapesApp::BuildConstantBufferViews() {
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT objCount = (UINT)mAllRitems.size();
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
    CD3DX12_DESCRIPTOR_RANGE cbvTable0, cbvTable1, texTable;
    cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[3];
    // Root parameter order (matches Draw()):
    //  slot 0 -> cbPerObject (register b0)
    //  slot 1 -> cbPass      (register b1)
    //  slot 2 -> texture SRV table used by the pixel shader (register t0)
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
    slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

    slotRootParameter[2].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC sampler0(
        0, // shader register s0
        D3D12_FILTER_ANISOTROPIC,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        0.0f,
        8);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, 1, &sampler0, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> serializedRootSig = nullptr, errorBlob = nullptr;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(md3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShadersAndInputLayout() {
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_1");
    mShaders["treeVS"] = d3dUtil::CompileShader(L"Shaders\\treeBillboard.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["treeGS"] = d3dUtil::CompileShader(L"Shaders\\treeBillboard.hlsl", nullptr, "GS", "gs_5_1");
    mShaders["treePS"] = d3dUtil::CompileShader(L"Shaders\\treeBillboard.hlsl", nullptr, "PS", "ps_5_1");
    mInputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void ShapesApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    // Shared primitive meshes; each RenderItem picks a texture via TexSrvHeapIndex.
    GeometryGenerator::MeshData stoneBox = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData woodBox = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(30.0f, 30.0f, 60, 40);
    GeometryGenerator::MeshData waterGrid = geoGen.CreateGrid(60.0f, 60.0f, 80, 80);
    // Higher tessellation so tower tops read as spheres (not faceted cones).
    GeometryGenerator::MeshData sphereLeaves = geoGen.CreateSphere(0.5f, 30, 30);
    GeometryGenerator::MeshData sphereRoof = geoGen.CreateSphere(0.5f, 30, 30);
    GeometryGenerator::MeshData cylinderTower = geoGen.CreateCylinder(0.5f, 0.5f, 3.0f, 24, 10);
    GeometryGenerator::MeshData cylinderTrunk = geoGen.CreateCylinder(0.5f, 0.5f, 3.0f, 24, 10);
    GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 1.0f);

    // Extra primitives for walls and props.
    GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f);
    GeometryGenerator::MeshData cone = geoGen.CreateCone(0.5f, 1.5f, 20);
    GeometryGenerator::MeshData triPrism = geoGen.CreateTriangularPrism(1.0f, 1.0f, 1.0f);
    GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.0f, 1.0f);

    UINT stoneBoxVertexOffset = 0;
    UINT woodBoxVertexOffset = stoneBoxVertexOffset + (UINT)stoneBox.Vertices.size();
    UINT gridVertexOffset = woodBoxVertexOffset + (UINT)woodBox.Vertices.size();
    UINT waterGridVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT sphereLeavesVertexOffset = waterGridVertexOffset + (UINT)waterGrid.Vertices.size();
    UINT sphereRoofVertexOffset = sphereLeavesVertexOffset + (UINT)sphereLeaves.Vertices.size();
    UINT cylinderTowerVertexOffset = sphereRoofVertexOffset + (UINT)sphereRoof.Vertices.size();
    UINT cylinderTrunkVertexOffset = cylinderTowerVertexOffset + (UINT)cylinderTower.Vertices.size();
    UINT pyramidVertexOffset = cylinderTrunkVertexOffset + (UINT)cylinderTrunk.Vertices.size();
    UINT wedgeVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
    UINT coneVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
    UINT triPrismVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
    UINT diamondVertexOffset = triPrismVertexOffset + (UINT)triPrism.Vertices.size();

    UINT stoneBoxIndexOffset = 0;
    UINT woodBoxIndexOffset = stoneBoxIndexOffset + (UINT)stoneBox.Indices32.size();
    UINT gridIndexOffset = woodBoxIndexOffset + (UINT)woodBox.Indices32.size();
    UINT waterGridIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT sphereLeavesIndexOffset = waterGridIndexOffset + (UINT)waterGrid.Indices32.size();
    UINT sphereRoofIndexOffset = sphereLeavesIndexOffset + (UINT)sphereLeaves.Indices32.size();
    UINT cylinderTowerIndexOffset = sphereRoofIndexOffset + (UINT)sphereRoof.Indices32.size();
    UINT cylinderTrunkIndexOffset = cylinderTowerIndexOffset + (UINT)cylinderTower.Indices32.size();
    UINT pyramidIndexOffset = cylinderTrunkIndexOffset + (UINT)cylinderTrunk.Indices32.size();
    UINT wedgeIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
    UINT coneIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
    UINT triPrismIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
    UINT diamondIndexOffset = triPrismIndexOffset + (UINT)triPrism.Indices32.size();

    SubmeshGeometry stoneBoxSubmesh; stoneBoxSubmesh.IndexCount = (UINT)stoneBox.Indices32.size(); stoneBoxSubmesh.StartIndexLocation = stoneBoxIndexOffset; stoneBoxSubmesh.BaseVertexLocation = stoneBoxVertexOffset;
    SubmeshGeometry woodBoxSubmesh; woodBoxSubmesh.IndexCount = (UINT)woodBox.Indices32.size(); woodBoxSubmesh.StartIndexLocation = woodBoxIndexOffset; woodBoxSubmesh.BaseVertexLocation = woodBoxVertexOffset;
    SubmeshGeometry gridSubmesh; gridSubmesh.IndexCount = (UINT)grid.Indices32.size(); gridSubmesh.StartIndexLocation = gridIndexOffset; gridSubmesh.BaseVertexLocation = gridVertexOffset;
    SubmeshGeometry waterGridSubmesh; waterGridSubmesh.IndexCount = (UINT)waterGrid.Indices32.size(); waterGridSubmesh.StartIndexLocation = waterGridIndexOffset; waterGridSubmesh.BaseVertexLocation = waterGridVertexOffset;
    SubmeshGeometry sphereLeavesSubmesh; sphereLeavesSubmesh.IndexCount = (UINT)sphereLeaves.Indices32.size(); sphereLeavesSubmesh.StartIndexLocation = sphereLeavesIndexOffset; sphereLeavesSubmesh.BaseVertexLocation = sphereLeavesVertexOffset;
    SubmeshGeometry sphereRoofSubmesh; sphereRoofSubmesh.IndexCount = (UINT)sphereRoof.Indices32.size(); sphereRoofSubmesh.StartIndexLocation = sphereRoofIndexOffset; sphereRoofSubmesh.BaseVertexLocation = sphereRoofVertexOffset;
    SubmeshGeometry cylinderTowerSubmesh; cylinderTowerSubmesh.IndexCount = (UINT)cylinderTower.Indices32.size(); cylinderTowerSubmesh.StartIndexLocation = cylinderTowerIndexOffset; cylinderTowerSubmesh.BaseVertexLocation = cylinderTowerVertexOffset;
    SubmeshGeometry cylinderTrunkSubmesh; cylinderTrunkSubmesh.IndexCount = (UINT)cylinderTrunk.Indices32.size(); cylinderTrunkSubmesh.StartIndexLocation = cylinderTrunkIndexOffset; cylinderTrunkSubmesh.BaseVertexLocation = cylinderTrunkVertexOffset;
    SubmeshGeometry pyramidSubmesh; pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size(); pyramidSubmesh.StartIndexLocation = pyramidIndexOffset; pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;
    SubmeshGeometry wedgeSubmesh; wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size(); wedgeSubmesh.StartIndexLocation = wedgeIndexOffset; wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;
    SubmeshGeometry coneSubmesh; coneSubmesh.IndexCount = (UINT)cone.Indices32.size(); coneSubmesh.StartIndexLocation = coneIndexOffset; coneSubmesh.BaseVertexLocation = coneVertexOffset;
    SubmeshGeometry triPrismSubmesh; triPrismSubmesh.IndexCount = (UINT)triPrism.Indices32.size(); triPrismSubmesh.StartIndexLocation = triPrismIndexOffset; triPrismSubmesh.BaseVertexLocation = triPrismVertexOffset;
    SubmeshGeometry diamondSubmesh; diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size(); diamondSubmesh.StartIndexLocation = diamondIndexOffset; diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

    auto totalVertexCount = stoneBox.Vertices.size() + woodBox.Vertices.size() + grid.Vertices.size() + waterGrid.Vertices.size() +
        sphereLeaves.Vertices.size() + sphereRoof.Vertices.size() + cylinderTower.Vertices.size() + cylinderTrunk.Vertices.size() +
        pyramid.Vertices.size() +
        wedge.Vertices.size() + cone.Vertices.size() + triPrism.Vertices.size() + diamond.Vertices.size();
    std::vector<Vertex> vertices(totalVertexCount);
    UINT k = 0;
    for (size_t i = 0; i < stoneBox.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = stoneBox.Vertices[i].Position;
        vertices[k].Normal = stoneBox.Vertices[i].Normal;
        vertices[k].TexC = stoneBox.Vertices[i].TexC;
    }
    for (size_t i = 0; i < woodBox.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = woodBox.Vertices[i].Position;
        vertices[k].Normal = woodBox.Vertices[i].Normal;
        vertices[k].TexC = woodBox.Vertices[i].TexC;
    }
    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexC;
    }
    for (size_t i = 0; i < waterGrid.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = waterGrid.Vertices[i].Position;
        vertices[k].Normal = waterGrid.Vertices[i].Normal;
        vertices[k].TexC = waterGrid.Vertices[i].TexC;
    }
    for (size_t i = 0; i < sphereLeaves.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = sphereLeaves.Vertices[i].Position;
        vertices[k].Normal = sphereLeaves.Vertices[i].Normal;
        vertices[k].TexC = sphereLeaves.Vertices[i].TexC;
    }
    for (size_t i = 0; i < sphereRoof.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = sphereRoof.Vertices[i].Position;
        vertices[k].Normal = sphereRoof.Vertices[i].Normal;
        vertices[k].TexC = sphereRoof.Vertices[i].TexC;
    }
    for (size_t i = 0; i < cylinderTower.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = cylinderTower.Vertices[i].Position;
        vertices[k].Normal = cylinderTower.Vertices[i].Normal;
        vertices[k].TexC = cylinderTower.Vertices[i].TexC;
    }
    for (size_t i = 0; i < cylinderTrunk.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = cylinderTrunk.Vertices[i].Position;
        vertices[k].Normal = cylinderTrunk.Vertices[i].Normal;
        vertices[k].TexC = cylinderTrunk.Vertices[i].TexC;
    }
    for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = pyramid.Vertices[i].Position;
        vertices[k].Normal = pyramid.Vertices[i].Normal;
        vertices[k].TexC = pyramid.Vertices[i].TexC;
    }
    for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = wedge.Vertices[i].Position;
        vertices[k].Normal = wedge.Vertices[i].Normal;
        vertices[k].TexC = wedge.Vertices[i].TexC;
    }
    for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = cone.Vertices[i].Position;
        vertices[k].Normal = cone.Vertices[i].Normal;
        vertices[k].TexC = cone.Vertices[i].TexC;
    }
    for (size_t i = 0; i < triPrism.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = triPrism.Vertices[i].Position;
        vertices[k].Normal = triPrism.Vertices[i].Normal;
        vertices[k].TexC = triPrism.Vertices[i].TexC;
    }
    for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = diamond.Vertices[i].Position;
        vertices[k].Normal = diamond.Vertices[i].Normal;
        vertices[k].TexC = diamond.Vertices[i].TexC;
    }

    std::vector<std::uint16_t> indices;
    for (auto i : stoneBox.Indices32) indices.push_back((uint16_t)i);
    for (auto i : woodBox.Indices32) indices.push_back((uint16_t)(i));
    for (auto i : grid.Indices32) indices.push_back((uint16_t)i);
    for (auto i : waterGrid.Indices32) indices.push_back((uint16_t)i);
    for (auto i : sphereLeaves.Indices32) indices.push_back((uint16_t)i);
    for (auto i : sphereRoof.Indices32) indices.push_back((uint16_t)i);
    for (auto i : cylinderTower.Indices32) indices.push_back((uint16_t)i);
    for (auto i : cylinderTrunk.Indices32) indices.push_back((uint16_t)i);
    for (auto i : pyramid.Indices32) indices.push_back((uint16_t)i);
    for (auto i : wedge.Indices32) indices.push_back((uint16_t)i);
    for (auto i : cone.Indices32) indices.push_back((uint16_t)i);
    for (auto i : triPrism.Indices32) indices.push_back((uint16_t)i);
    for (auto i : diamond.Indices32) indices.push_back((uint16_t)i);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";
    ThrowIfFailed(D3DCreateBlob(vertices.size() * sizeof(Vertex), &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertices.size() * sizeof(Vertex));
    ThrowIfFailed(D3DCreateBlob(indices.size() * sizeof(std::uint16_t), &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), indices.size() * sizeof(std::uint16_t));
    // Vertex TexC is [0,1] on the grid; tiling for grass etc. comes from RenderItem::TexTransform → cbPerObject (Luna Week 7).
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vertices.data(), vertices.size() * sizeof(Vertex), geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), indices.data(), indices.size() * sizeof(std::uint16_t), geo->IndexBufferUploader);
    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));

    geo->DrawArgs["stoneBox"] = stoneBoxSubmesh;
    geo->DrawArgs["woodBox"] = woodBoxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["waterGrid"] = waterGridSubmesh;
    geo->DrawArgs["sphereLeaves"] = sphereLeavesSubmesh;
    geo->DrawArgs["sphereRoof"] = sphereRoofSubmesh;
    geo->DrawArgs["cylinderTower"] = cylinderTowerSubmesh;
    geo->DrawArgs["cylinderTrunk"] = cylinderTrunkSubmesh;
    geo->DrawArgs["pyramid"] = pyramidSubmesh;
    geo->DrawArgs["wedge"] = wedgeSubmesh;
    geo->DrawArgs["cone"] = coneSubmesh;
    geo->DrawArgs["triPrism"] = triPrismSubmesh;
    geo->DrawArgs["diamond"] = diamondSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildSkullTxtGeometry()
{
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring exeDir(modulePath);
    const size_t slash = exeDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        exeDir.resize(slash + 1);

    const std::wstring candidates[] = {
        exeDir + L"Models\\skull.txt",
        L"Models\\skull.txt",
        L"..\\..\\Models\\skull.txt",
        L"e:\\HOOMAN\\DirectX-master\\Week12\\Picking\\Models\\skull.txt",
    };

    for (const auto& path : candidates) {
        std::unique_ptr<MeshGeometry> geo;
        if (::BuildSkullTxtGeometry(md3dDevice.Get(), mCommandList.Get(), path, geo)) {
            mGeometries["skullGeo"] = std::move(geo);
            return;
        }
    }
}

void ShapesApp::BuildTreeBillboardGeometry()
{
    // Each vertex is one tree root. TexC.xy = half width and half height in world units (read in treeBillboard VS/GS).
    const int numTrees = kBillboardTreeCount;
    std::vector<Vertex> vertices(numTrees);
    std::vector<std::uint16_t> indices(numTrees);
    for (int i = 0; i < numTrees; ++i) {
        vertices[i].Pos = { 0.0f, 0.0f, 0.0f };
        vertices[i].Normal = { 0.0f, 0.0f, 0.0f };
        vertices[i].TexC = { kTreeBillboardHalfW, kTreeBillboardHalfH };
        indices[i] = (std::uint16_t)i;
    }

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "treeBillboardGeo";
    ThrowIfFailed(D3DCreateBlob(vertices.size() * sizeof(Vertex), &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertices.size() * sizeof(Vertex));
    ThrowIfFailed(D3DCreateBlob(indices.size() * sizeof(std::uint16_t), &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), indices.size() * sizeof(std::uint16_t));
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vertices.data(), vertices.size() * sizeof(Vertex), geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), indices.data(), indices.size() * sizeof(std::uint16_t), geo->IndexBufferUploader);
    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));

    SubmeshGeometry onePoint;
    onePoint.IndexCount = 1;
    onePoint.StartIndexLocation = 0;
    onePoint.BaseVertexLocation = 0;
    geo->DrawArgs["treePoint"] = onePoint;

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

    // Transparent PSO: standard alpha blend; depth writes off so multiple water layers can draw over each other.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;
    D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
    transparencyBlendDesc.BlendEnable = true;
    transparencyBlendDesc.LogicOpEnable = false;
    transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
    transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
    // Transparent objects should not write depth; otherwise, their depth can
    // incorrectly occlude other transparent surfaces (ex: moat water).
    transparentPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    // Keep depth testing enabled so transparent water blends with the
    // trench bottom (black) rather than with the green ground.
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC treePsoDesc;
    ZeroMemory(&treePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    treePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    treePsoDesc.pRootSignature = mRootSignature.Get();
    treePsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["treeVS"]->GetBufferPointer()), mShaders["treeVS"]->GetBufferSize() };
    treePsoDesc.GS = { reinterpret_cast<BYTE*>(mShaders["treeGS"]->GetBufferPointer()), mShaders["treeGS"]->GetBufferSize() };
    treePsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["treePS"]->GetBufferPointer()), mShaders["treePS"]->GetBufferSize() };
    treePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    treePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    treePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    treePsoDesc.SampleMask = UINT_MAX;
    treePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    treePsoDesc.NumRenderTargets = 1;
    treePsoDesc.RTVFormats[0] = mBackBufferFormat;
    treePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    treePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treePsoDesc, IID_PPV_ARGS(&mPSOs["tree_billboard"])));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC treeWirePsoDesc = treePsoDesc;
    treeWirePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeWirePsoDesc, IID_PPV_ARGS(&mPSOs["tree_billboard_wireframe"])));
}

void ShapesApp::BuildFrameResources() {
    // Allocate constant buffers large enough for *all* render items.
    // If this is too small, CBV creation can point at memory outside the upload heap,
    // causing D3D12 ERROR #649 / invalid BufferLocation.
    const UINT objCount = static_cast<UINT>(mAllRitems.size());
    for (int i = 0; i < gNumFrameResources; ++i)
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, objCount));
}

void ShapesApp::BuildRenderItems()
{
    UINT objCBIndex = 0;

    // Mesh names in shapeGeo (diamond, cone, etc.) are used for fountain, towers, gate, etc.

    // Moat water: eight scaled waterGrid pieces (four strips + four corners) around the yard; constants set channel width.
    const float moatInner = 15.0f; // inner edge (gap from walls)
    const float moatOuter = 20.0f; // outer edge
    const float moatThickness = moatOuter - moatInner;
    const float waterBaseSize = 60.0f; // GeometryGenerator CreateGrid(60,60,...)

    const float centerEdge = 0.5f * (moatInner + moatOuter); // (12.5 + 20)/2 = 16.25
    const float sizeInnerX = 2.0f * moatInner;               // 25.0
    const float sizeOuterStripZ = moatThickness;            // 7.5
    const float stripScaleX = sizeInnerX / waterBaseSize;
    const float stripScaleZ = sizeOuterStripZ / waterBaseSize;

    const float cornerScale = moatThickness / waterBaseSize;

    // 1. SANDWICH GROUND LAYOUT
    // - center yard grass
    // - black trench + water in the moat region (handled below)
    // - outer grass around the moat; island edge (outerBound) meets ocean — trees sit just inside that edge.
    const float outerBound = 58.0f; // outer edge of grass (island + margin past maze approach)
    const float yardHalf = moatInner - 0.75f; // leave a small gap so no grass lies under the moat cutout
    const float moatEdgeMid = 0.5f * (moatOuter + outerBound);
    const float outerStripHalfZ = moatEdgeMid;
    const float outerStripHalfX = moatEdgeMid;

    // Grass: fewer UV repeats + strong V-stretch = long blade look; wild tint for color.hlsl grass path.
    const float refGrassScale = (2.0f * yardHalf) / waterBaseSize;
    const float kGrassRepeatsRef = 7.25f;
    const float kGrassUvStretchU = 0.62f;
    const float kGrassUvStretchV = 1.62f;

    auto addGrassTile = [&](float sx, float sz, float x, float z, float yElev)
    {
        auto r = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&r->World,
            XMMatrixScaling(sx, 1.0f, sz) *
            XMMatrixTranslation(x, yElev, z));
        const float baseU = kGrassRepeatsRef * (sx / refGrassScale);
        const float baseV = kGrassRepeatsRef * (sz / refGrassScale);
        const float tu = baseU * kGrassUvStretchU;
        const float tv = baseV * kGrassUvStretchV;
        XMMATRIX grassTex = XMMatrixScaling(tu, tv, 1.0f);
        XMStoreFloat4x4(&r->TexTransform, grassTex);
        r->BaseColorMul = XMFLOAT3(0.52f, 1.32f, 0.42f);

        r->ObjCBIndex = objCBIndex++;
        r->Geo = mGeometries["shapeGeo"].get();
        r->IndexCount = r->Geo->DrawArgs["waterGrid"].IndexCount;
        r->StartIndexLocation = r->Geo->DrawArgs["waterGrid"].StartIndexLocation;
        r->BaseVertexLocation = r->Geo->DrawArgs["waterGrid"].BaseVertexLocation;
        r->TexSrvHeapIndex = TexGrass;
        mAllRitems.push_back(std::move(r));
    };
    auto makeGroundTile = [&](float sx, float sz, float x, float z)
    {
        addGrassTile(sx, sz, x, z, 0.0f);
    };

    // Center yard grass square
    makeGroundTile((2.0f * yardHalf) / waterBaseSize, (2.0f * yardHalf) / waterBaseSize, 0.0f, 0.0f);

    // Outer grass ring tiles (outside moatOuter)
    const float outerGrassWidthX = (outerBound - moatOuter) / waterBaseSize;
    const float outerGrassWidthZ = (outerBound - moatOuter) / waterBaseSize;
    const float outerGrassHalfInnerZ = moatOuter / waterBaseSize;

    // North & South strips
    makeGroundTile((2.0f * outerBound) / waterBaseSize, (outerBound - moatOuter) / waterBaseSize, 0.0f, +moatEdgeMid);
    makeGroundTile((2.0f * outerBound) / waterBaseSize, (outerBound - moatOuter) / waterBaseSize, 0.0f, -moatEdgeMid);

    // East & West strips
    makeGroundTile((outerBound - moatOuter) / waterBaseSize, (2.0f * moatOuter) / waterBaseSize, +moatEdgeMid, 0.0f);
    makeGroundTile((outerBound - moatOuter) / waterBaseSize, (2.0f * moatOuter) / waterBaseSize, -moatEdgeMid, 0.0f);

    // Corners (optional but helps fill the ring corners)
    makeGroundTile((outerBound - moatOuter) / waterBaseSize, (outerBound - moatOuter) / waterBaseSize, +moatEdgeMid, +moatEdgeMid);
    makeGroundTile((outerBound - moatOuter) / waterBaseSize, (outerBound - moatOuter) / waterBaseSize, +moatEdgeMid, -moatEdgeMid);
    makeGroundTile((outerBound - moatOuter) / waterBaseSize, (outerBound - moatOuter) / waterBaseSize, -moatEdgeMid, +moatEdgeMid);
    makeGroundTile((outerBound - moatOuter) / waterBaseSize, (outerBound - moatOuter) / waterBaseSize, -moatEdgeMid, -moatEdgeMid);

    // Grass strip south of the maze entrance (extends the green line toward the player past the maze mouth).
    {
        using namespace MazeConfig;
        const float mazeHalfW = 0.5f * kCols * kCell;
        const float southPad = 28.0f;
        const float stripZ = southPad / waterBaseSize;
        const float stripX = (2.0f * mazeHalfW + 16.0f) / waterBaseSize;
        const float southZ = kOriginZ - 0.5f * southPad - 0.35f * kCell;
        makeGroundTile(stripX, stripZ, 0.0f, southZ);
    }

    // Continuous grass across the entire maze (slightly above Y=0 to limit z-fighting with the yard tiles).
    {
        using namespace MazeConfig;
        const float pad = 4.0f;
        const float gw = (kCols * kCell + pad) / waterBaseSize;
        const float gd = (kRows * kCell + pad) / waterBaseSize;
        const float cx = kOriginX + 0.5f * kCols * kCell;
        const float cz = kOriginZ + 0.5f * kRows * kCell;
        addGrassTile(gw, gd, cx, cz, 0.018f);
    }

    // Trench geometry: opaque side walls + bottom floor.
    // Water surface sits inside the trench.
    const float waterSurfaceY = -0.18f;
    const float trenchBottomY = -0.55f;
    const float trenchWallTopY = 0.0f; // ground level
    const float trenchWallHeight = trenchWallTopY - trenchBottomY;
    const float trenchWallCenterY = trenchBottomY + 0.5f * trenchWallHeight;

    // Water surface: Alpha controls opacity in the shader; tint and scrolling are in Shaders/color.hlsl (pixel shader).
    auto makeWater = [&](float sx, float sz, float x, float z)
    {
        auto r = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&r->World,
            XMMatrixScaling(sx, 0.03f, sz) *
            XMMatrixTranslation(x, waterSurfaceY, z));
        r->Alpha = 0.89f;
        r->WaterSurfaceUv = 1.0f;
        r->ObjCBIndex = objCBIndex++;
        r->Geo = mGeometries["shapeGeo"].get();
        r->IndexCount = r->Geo->DrawArgs["waterGrid"].IndexCount;
        r->StartIndexLocation = r->Geo->DrawArgs["waterGrid"].StartIndexLocation;
        r->BaseVertexLocation = r->Geo->DrawArgs["waterGrid"].BaseVertexLocation;
        r->TexSrvHeapIndex = TexWater;
        mAllRitems.push_back(std::move(r));
    };

    auto makeMoatFloor = [&](float sx, float sz, float x, float z)
    {
        auto r = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&r->World,
            XMMatrixScaling(sx, 0.02f, sz) *
            XMMatrixTranslation(x, trenchBottomY, z));
        r->ObjCBIndex = objCBIndex++;
        r->Geo = mGeometries["shapeGeo"].get();
        r->IndexCount = r->Geo->DrawArgs["waterGrid"].IndexCount;
        r->StartIndexLocation = r->Geo->DrawArgs["waterGrid"].StartIndexLocation;
        r->BaseVertexLocation = r->Geo->DrawArgs["waterGrid"].BaseVertexLocation;
        // Stone floor so the trench reads as a cutout (not another water puddle).
        r->TexSrvHeapIndex = TexStoneNeck;
        // Force trench bottom to render black to avoid green-ground reflections through the water.
        r->BaseColorMul = XMFLOAT3(0.0f, 0.0f, 0.0f);
        mAllRitems.push_back(std::move(r));
    };

    // North strip
    makeWater(stripScaleX, stripScaleZ, 0.0f, +centerEdge);
    makeMoatFloor(stripScaleX, stripScaleZ, 0.0f, +centerEdge);
    // South strip
    makeWater(stripScaleX, stripScaleZ, 0.0f, -centerEdge);
    makeMoatFloor(stripScaleX, stripScaleZ, 0.0f, -centerEdge);
    // East strip
    makeWater(stripScaleZ, stripScaleX, +centerEdge, 0.0f);
    makeMoatFloor(stripScaleZ, stripScaleX, +centerEdge, 0.0f);
    // West strip
    makeWater(stripScaleZ, stripScaleX, -centerEdge, 0.0f);
    makeMoatFloor(stripScaleZ, stripScaleX, -centerEdge, 0.0f);

    // Corner blocks
    makeWater(cornerScale, cornerScale, +centerEdge, +centerEdge);
    makeMoatFloor(cornerScale, cornerScale, +centerEdge, +centerEdge);
    makeWater(cornerScale, cornerScale, +centerEdge, -centerEdge);
    makeMoatFloor(cornerScale, cornerScale, +centerEdge, -centerEdge);
    makeWater(cornerScale, cornerScale, -centerEdge, +centerEdge);
    makeMoatFloor(cornerScale, cornerScale, -centerEdge, +centerEdge);
    makeWater(cornerScale, cornerScale, -centerEdge, -centerEdge);
    makeMoatFloor(cornerScale, cornerScale, -centerEdge, -centerEdge);

    // Outer ocean: oceanInnerEdge matches outerBound so grass meets water with no gap.
    const float oceanInnerEdge = outerBound;
    const float oceanOuterEdge = 128.0f; // widened with island so open water still reads clearly beyond the trees
    const float oceanMid = 0.5f * (oceanInnerEdge + oceanOuterEdge);
    const float oceanThickness = oceanOuterEdge - oceanInnerEdge;
    const float stripScaleXO = (2.0f * oceanOuterEdge) / waterBaseSize;
    const float stripScaleZO = oceanThickness / waterBaseSize;
    const float cornerScaleO = oceanThickness / waterBaseSize;

    makeWater(stripScaleXO, stripScaleZO, 0.0f, +oceanMid);
    makeWater(stripScaleXO, stripScaleZO, 0.0f, -oceanMid);
    makeWater(stripScaleZO, stripScaleXO, +oceanMid, 0.0f);
    makeWater(stripScaleZO, stripScaleXO, -oceanMid, 0.0f);
    makeWater(cornerScaleO, cornerScaleO, +oceanMid, +oceanMid);
    makeWater(cornerScaleO, cornerScaleO, +oceanMid, -oceanMid);
    makeWater(cornerScaleO, cornerScaleO, -oceanMid, +oceanMid);
    makeWater(cornerScaleO, cornerScaleO, -oceanMid, -oceanMid);

    // Pool at the maze south approach (main “maze entrance” water; moat/ocean remain for the island).
    {
        using namespace MazeConfig;
        const float mazeHalfW = 0.5f * kCols * kCell;
        const float southPad = 28.0f;
        const float poolW = (2.0f * mazeHalfW + 14.0f) / waterBaseSize;
        const float poolD = 20.0f / waterBaseSize;
        const float poolZ = kOriginZ - 0.5f * southPad - 0.25f * kCell;
        makeWater(poolW, poolD, 0.0f, poolZ);
        makeMoatFloor(poolW, poolD, 0.0f, poolZ);
    }

    // Maze (brick walls + axis-aligned colliders for first-person walking).
    {
        using namespace MazeConfig;
        mMazeColliders.clear();
        const float sx = kCell * kWallScaleZ;
        const float sz = kCell * kWallScaleZ;
        const float sy = kWallHeight;
        for (int r = 0; r < kRows; ++r) {
            for (int c = 0; c < kCols; ++c) {
                if (kLines[r][c] != '#')
                    continue;
                const float wx = kOriginX + (c + 0.5f) * kCell;
                float wz = kOriginZ + (r + 0.5f) * kCell;
                if (r == kRows - 1)
                    wz += kLastRowZOffsetTowardCastle;
                const float wy = 0.5f * kWallHeight;

                AxisAlignedCollider box;
                box.Center = XMFLOAT3(wx, wy, wz);
                box.HalfExtents = XMFLOAT3(0.5f * sx, 0.5f * sy, 0.5f * sz);
                mMazeColliders.push_back(box);

                auto wall = std::make_unique<RenderItem>();
                XMStoreFloat4x4(&wall->World,
                    XMMatrixScaling(sx, sy, sz) * XMMatrixTranslation(wx, wy, wz));
                wall->ObjCBIndex = objCBIndex++;
                wall->Geo = mGeometries["shapeGeo"].get();
                wall->IndexCount = wall->Geo->DrawArgs["stoneBox"].IndexCount;
                wall->StartIndexLocation = wall->Geo->DrawArgs["stoneBox"].StartIndexLocation;
                wall->BaseVertexLocation = wall->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
                wall->TexSrvHeapIndex = TexBrickBase;
                mAllRitems.push_back(std::move(wall));
            }
        }

        // Maze dressing: Week 7-style TexTransform/BaseColorMul on select props; optional yaw + colliders.
        auto addMazeProp = [&](float cx, float cz, float yBottom, float sx, float sy, float sz,
            const char* meshName, UINT texSlot, bool addCollider, float yaw = 0.f,
            const XMFLOAT3* pMul = nullptr, const XMMATRIX* pTex = nullptr)
        {
            const float cy = yBottom + 0.5f * sy;
            auto p = std::make_unique<RenderItem>();
            XMMATRIX world = XMMatrixScaling(sx, sy, sz) * XMMatrixRotationY(yaw) * XMMatrixTranslation(cx, cy, cz);
            XMStoreFloat4x4(&p->World, world);
            if (pMul)
                p->BaseColorMul = *pMul;
            if (pTex)
                XMStoreFloat4x4(&p->TexTransform, *pTex);
            p->ObjCBIndex = objCBIndex++;
            p->Geo = mGeometries["shapeGeo"].get();
            const auto& sm = p->Geo->DrawArgs[meshName];
            p->IndexCount = sm.IndexCount;
            p->StartIndexLocation = sm.StartIndexLocation;
            p->BaseVertexLocation = sm.BaseVertexLocation;
            p->TexSrvHeapIndex = texSlot;
            if (addCollider) {
                AxisAlignedCollider b;
                b.Center = XMFLOAT3(cx, cy, cz);
                b.HalfExtents = XMFLOAT3(0.5f * sx, 0.5f * sy, 0.5f * sz);
                mMazeColliders.push_back(b);
            }
            mAllRitems.push_back(std::move(p));
        };

        auto addSkullMeshPropNS = [&](float cx, float cz, float yBottom, float sx, float sy, float sz, float yawRad, bool addCollider)
        {
            auto it = mGeometries.find("skullGeo");
            if (it == mGeometries.end() || !it->second)
                return;
            auto p = std::make_unique<RenderItem>();
            XMMATRIX world = XMMatrixScaling(sx, sy, sz) * XMMatrixRotationY(yawRad) * XMMatrixTranslation(cx, yBottom, cz);
            XMStoreFloat4x4(&p->World, world);
            p->BaseColorMul = XMFLOAT3(0.9f, 0.88f, 0.84f);
            p->ObjCBIndex = objCBIndex++;
            p->Geo = it->second.get();
            const auto& sm = p->Geo->DrawArgs["skull"];
            p->IndexCount = sm.IndexCount;
            p->StartIndexLocation = sm.StartIndexLocation;
            p->BaseVertexLocation = sm.BaseVertexLocation;
            p->TexSrvHeapIndex = TexW12Skull;
            if (addCollider) {
                const float hx = 0.24f * sx;
                const float hy = 0.24f * sy;
                const float hz = 0.24f * sz;
                AxisAlignedCollider b;
                b.Center = XMFLOAT3(cx, yBottom + hy, cz);
                b.HalfExtents = XMFLOAT3(hx, hy, hz);
                mMazeColliders.push_back(b);
            }
            mAllRitems.push_back(std::move(p));
        };

        auto addSkullMeshProp = [&](float cx, float cz, float yBottom, float uniformScale, float yawRad, bool addCollider)
        {
            addSkullMeshPropNS(cx, cz, yBottom, uniformScale, uniformScale, uniformScale, yawRad, addCollider);
        };

        auto cellXZ = [&](int row, int col) -> XMFLOAT2
        {
            float z = kOriginZ + (row + 0.5f) * kCell;
            if (row == kRows - 1)
                z += kLastRowZOffsetTowardCastle;
            return XMFLOAT2(kOriginX + (col + 0.5f) * kCell, z);
        };

        auto addPodium = [&](float cx, float cz)
        {
            // Wider, taller cylinder; slim skull (non-uniform scale) on top.
            addMazeProp(cx, cz, 0.0f, 0.82f, 0.58f, 0.82f, "cylinderTrunk", TexStoneNeck, true, 0.f, nullptr, nullptr);
            if (mGeometries.find("skullGeo") != mGeometries.end() && mGeometries["skullGeo"])
                addSkullMeshPropNS(cx, cz, 0.58f, 0.095f, 0.155f, 0.088f, 0.f, false);
            else
                addMazeProp(cx, cz, 0.58f, 0.48f, 0.34f, 0.44f, "stoneBox", TexW12Skull, true, 0.f, nullptr, nullptr);
        };

        // Stone spikes in open corridors with skull trophies (cone + mesh or fallback box).
        auto addSpikeTrophy = [&](float cx, float cz, float yawRad)
        {
            const float spH = 1.48f;
            const float spR = 0.14f;
            addMazeProp(cx, cz, 0.0f, spR, spH, spR, "cone", TexBrickBase, true, yawRad);
            if (mGeometries.find("skullGeo") != mGeometries.end() && mGeometries["skullGeo"])
                addSkullMeshPropNS(cx, cz, spH, 0.092f, 0.14f, 0.085f, yawRad + 0.35f, false);
            else
                addMazeProp(cx, cz, spH, 0.26f, 0.28f, 0.24f, "stoneBox", TexW12Skull, false, yawRad);
        };

        // Skull / crossbones texture on thin wall slabs (2D “drawings” along corridors).
        {
            XMFLOAT2 q = cellXZ(12, 11);
            addMazeProp(q.x - 0.82f, q.y, 1.1f, 0.06f, 0.95f, 0.68f, "stoneBox", TexW12Skull, false, 0.0f);
            addMazeProp(q.x + 0.82f, q.y, 1.1f, 0.06f, 0.95f, 0.68f, "stoneBox", TexW12Skull, false, 3.14159265f);
            q = cellXZ(16, 11);
            addMazeProp(q.x, q.y - 0.82f, 1.2f, 0.65f, 1.0f, 0.06f, "stoneBox", TexW12Skull, false, 1.57079633f);
            q = cellXZ(10, 15);
            addMazeProp(q.x + 0.78f, q.y, 0.95f, 0.06f, 0.85f, 0.6f, "stoneBox", TexW12Bones, false, 0.0f);
        }

        // Extra 3D skull on the path when skull.txt loaded (Week12 mesh).
        {
            XMFLOAT2 q = cellXZ(8, 9);
            addSkullMeshProp(q.x + 0.15f, q.y, 0.0f, 0.19f, -0.85f, true);
        }

        // Skull plaques + relics (row indices offset for +8 south extension rows).
        {
            XMFLOAT2 q = cellXZ(10, 11);
            addMazeProp(q.x + 0.7f, q.y, 0.0f, 0.55f, 0.4f, 0.32f, "stoneBox", TexW12Skull, true);
        }
        {
            XMFLOAT2 q = cellXZ(13, 7);
            addMazeProp(q.x - 0.5f, q.y + 0.4f, 0.0f, 0.5f, 0.35f, 0.5f, "stoneBox", TexW12Skull, true);
        }
        {
            XMFLOAT2 q = cellXZ(16, 15);
            addMazeProp(q.x, q.y, 0.0f, 0.65f, 0.55f, 0.65f, "pyramid", TexStone, true);
        }
        {
            XMFLOAT2 q = cellXZ(18, 10);
            addMazeProp(q.x + 0.45f, q.y - 0.45f, 0.0f, 0.9f, 0.35f, 0.55f, "wedge", TexBrickBase, true);
        }
        {
            XMFLOAT2 q = cellXZ(20, 5);
            addMazeProp(q.x, q.y, 0.0f, 0.35f, 0.95f, 0.35f, "cone", TexWood, true);
        }
        {
            XMFLOAT2 q = cellXZ(14, 4);
            addMazeProp(q.x, q.y, 0.0f, 0.32f, 0.45f, 0.32f, "cylinderTrunk", TexStoneNeck, true);
            addMazeProp(q.x, q.y, 0.45f, 0.5f, 0.22f, 0.42f, "stoneBox", TexW12Skull, true);
        }
        {
            XMFLOAT2 q = cellXZ(22, 12);
            addMazeProp(q.x - 0.55f, q.y, 0.0f, 0.45f, 0.5f, 0.45f, "triPrism", TexStoneNeck, true);
        }
        {
            XMFLOAT2 q = cellXZ(20, 11);
            addMazeProp(q.x + 0.5f, q.y, 0.0f, 0.55f, 0.28f, 0.4f, "stoneBox", TexW12Skull, true);
        }
        {
            XMFLOAT2 q = cellXZ(17, 17);
            addMazeProp(q.x, q.y, 0.0f, 0.5f, 0.15f, 0.85f, "stoneBox", TexBrickTop, false);
        }
        {
            XMFLOAT2 q = cellXZ(19, 3);
            static const XMFLOAT3 boneMul(0.95f, 0.72f, 0.52f);
            addMazeProp(q.x, q.y, 0.0f, 0.85f, 0.25f, 0.35f, "diamond", TexW12Bones, false, 0.4f, &boneMul, nullptr);
        }
        {
            XMFLOAT2 q = cellXZ(18, 10);
            addMazeProp(q.x, q.y, 0.0f, 0.4f, 0.4f, 0.4f, "pyramid", TexBrickTop, true);
        }

        // Bone pile (tinted bones texture).
        {
            XMFLOAT2 q = cellXZ(11, 6);
            static const XMFLOAT3 boneMul(0.92f, 0.68f, 0.48f);
            addMazeProp(q.x + 0.3f, q.y, 0.0f, 0.75f, 0.22f, 0.5f, "wedge", TexW12Bones, true, -0.5f, &boneMul, nullptr);
        }

        // Ritual “drawings” on the ground (flat diamonds, dark tint + UV scale).
        {
            XMMATRIX ritTex = XMMatrixScaling(2.8f, 2.8f, 1.0f);
            static const XMFLOAT3 ritMul(0.42f, 0.14f, 0.14f);
            XMFLOAT2 q = cellXZ(12, 8);
            addMazeProp(q.x, q.y, 0.02f, 1.15f, 0.04f, 1.15f, "diamond", TexW12Ritual, false, 0.7f, &ritMul, &ritTex);
            q = cellXZ(20, 14);
            addMazeProp(q.x, q.y, 0.02f, 1.0f, 0.04f, 1.0f, "diamond", TexW12Ritual, false, -0.9f, &ritMul, &ritTex);
            q = cellXZ(8, 14);
            addMazeProp(q.x, q.y, 0.02f, 0.9f, 0.04f, 0.9f, "diamond", TexW12Ritual, false, 1.2f, &ritMul, &ritTex);
        }

        // Podiums with slim skulls on larger bases.
        addPodium(cellXZ(15, 9).x, cellXZ(15, 9).y);
        addPodium(cellXZ(18, 6).x, cellXZ(18, 6).y);
        addPodium(cellXZ(21, 13).x, cellXZ(21, 13).y);

        // Spikes with trophy skulls (maze interior — fewer; more in courtyard toward moat).
        addSpikeTrophy(cellXZ(14, 10).x + 0.2f, cellXZ(14, 10).y - 0.15f, 0.9f);
        addSpikeTrophy(cellXZ(18, 12).x - 0.25f, cellXZ(18, 12).y + 0.1f, -0.4f);
        addSpikeTrophy(cellXZ(11, 13).x - 0.2f, cellXZ(11, 13).y + 0.25f, 2.5f);
        addSpikeTrophy(cellXZ(22, 10).x + 0.1f, cellXZ(22, 10).y, 0.65f);

        // Dead tree: gnarled trunk + crown + skulls on short “chains” (wood strips).
        {
            XMFLOAT2 base = cellXZ(15, 12);
            float bx = base.x;
            float bz = base.y;
            const float th = 3.85f;
            addMazeProp(bx, bz, 0.0f, 0.42f, th, 0.42f, "cylinderTrunk", TexWood, true);
            addMazeProp(bx, bz, th, 0.65f, 0.55f, 0.65f, "cone", TexWood, false);
            for (int h = 0; h < 6; ++h) {
                float t = h * 0.95f;
                float ang = h * 2.1f;
                float rx = 0.48f * cosf(ang);
                float rz = 0.48f * sinf(ang);
                float yb = 1.05f + t * 0.38f;
                addMazeProp(bx + rx, bz + rz, yb, 0.06f, 0.55f + h * 0.02f, 0.06f, "woodBox", TexWoodBridge, false);
                addMazeProp(bx + rx * 1.05f, bz + rz * 1.05f, yb - 0.35f, 0.24f, 0.26f, 0.22f, "stoneBox", TexW12Skull, false);
            }
        }
    }

    // Courtyard between maze north and castle moat: skull spikes + gallows (world space; not maze colliders).
    {
        using namespace MazeConfig;
        auto addCourtyardProp = [&](float cx, float cz, float yBottom, float sx, float sy, float sz,
            const char* meshName, UINT texSlot, float yaw = 0.f)
        {
            const float cy = yBottom + 0.5f * sy;
            auto p = std::make_unique<RenderItem>();
            XMMATRIX world = XMMatrixScaling(sx, sy, sz) * XMMatrixRotationY(yaw) * XMMatrixTranslation(cx, cy, cz);
            XMStoreFloat4x4(&p->World, world);
            p->ObjCBIndex = objCBIndex++;
            p->Geo = mGeometries["shapeGeo"].get();
            const auto& sm = p->Geo->DrawArgs[meshName];
            p->IndexCount = sm.IndexCount;
            p->StartIndexLocation = sm.StartIndexLocation;
            p->BaseVertexLocation = sm.BaseVertexLocation;
            p->TexSrvHeapIndex = texSlot;
            mAllRitems.push_back(std::move(p));
        };

        auto addCourtyardSpike = [&](float cx, float cz, float yawRad)
        {
            const float spH = 1.55f;
            const float spR = 0.15f;
            addCourtyardProp(cx, cz, 0.0f, spR, spH, spR, "cone", TexBrickBase, yawRad);
            auto it = mGeometries.find("skullGeo");
            if (it != mGeometries.end() && it->second) {
                auto p = std::make_unique<RenderItem>();
                XMMATRIX world = XMMatrixScaling(0.09f, 0.14f, 0.085f) * XMMatrixRotationY(yawRad + 0.4f) *
                    XMMatrixTranslation(cx, spH, cz);
                XMStoreFloat4x4(&p->World, world);
                p->BaseColorMul = XMFLOAT3(0.88f, 0.85f, 0.82f);
                p->ObjCBIndex = objCBIndex++;
                p->Geo = it->second.get();
                const auto& sm = p->Geo->DrawArgs["skull"];
                p->IndexCount = sm.IndexCount;
                p->StartIndexLocation = sm.StartIndexLocation;
                p->BaseVertexLocation = sm.BaseVertexLocation;
                p->TexSrvHeapIndex = TexW12Skull;
                mAllRitems.push_back(std::move(p));
            }
            else {
                addCourtyardProp(cx, cz, spH, 0.26f, 0.28f, 0.24f, "stoneBox", TexW12Skull, yawRad);
            }
        };

        // Courtyard props: Z at centre of last maze row (no hedge band).
        const float yardZ = kOriginZ + (static_cast<float>(kRows) - 0.5f) * kCell + kLastRowZOffsetTowardCastle;
        const float czc = yardZ;
        addCourtyardSpike(-13.5f, czc, 0.35f);
        addCourtyardSpike(-10.2f, czc, -0.25f);
        addCourtyardSpike(-7.2f, czc, 0.5f);
        addCourtyardSpike(7.2f, czc, -0.4f);
        addCourtyardSpike(10.2f, czc, 0.2f);
        addCourtyardSpike(13.5f, czc, -0.55f);

        // Gallows: H shape — beam is 6.2 wide in X; posts sit near ±half width so they meet the cross piece.
        const float gZ = yardZ - 1.05f * kCell;
        const float gXCross = -11.4f; // cross + rope + skull; nudged right vs old -12.5
        const float gHalfBeamX = 0.5f * 6.2f; // matches woodBox X scale
        const float gXLeftPost = gXCross - gHalfBeamX;
        const float gXRightPost = gXCross + gHalfBeamX;
        addCourtyardProp(gXLeftPost, gZ, 0.0f, 0.38f, 5.1f, 0.38f, "cylinderTrunk", TexWood, 0.f);
        addCourtyardProp(gXRightPost, gZ, 0.0f, 0.38f, 5.1f, 0.38f, "cylinderTrunk", TexWood, 0.f);
        addCourtyardProp(gXCross, gZ, 4.95f, 6.2f, 0.5f, 0.4f, "woodBox", TexWoodBridge, 0.f);
        addCourtyardProp(gXCross, gZ, 4.1f, 0.14f, 1.35f, 0.14f, "cylinderTrunk", TexWood, 0.f);
        addCourtyardProp(gXCross, gZ, 2.85f, 0.35f, 0.22f, 0.35f, "stoneBox", TexW12Skull, 0.15f);
    }

    // Center fountain: opaque stone, then transparent TexWater cones/cylinder (each has its own Alpha).

    // Basin (diamond, flattened)
    auto fountainBasin = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&fountainBasin->World,
        // Fountain basin (diamond) sits flat on the ground.
        // Scaled down by ~2x from the last oversized version.
        XMMatrixScaling(22.5f, 1.05f, 22.5f) *
        // Lift so basin sits above the ground surface.
        // Raise so the base is clearly above the ground.
        XMMatrixTranslation(0.0f, 2.28f, 0.0f));
    fountainBasin->ObjCBIndex = objCBIndex++;
    fountainBasin->Geo = mGeometries["shapeGeo"].get();
    fountainBasin->IndexCount = fountainBasin->Geo->DrawArgs["diamond"].IndexCount;
    fountainBasin->StartIndexLocation = fountainBasin->Geo->DrawArgs["diamond"].StartIndexLocation;
    fountainBasin->BaseVertexLocation = fountainBasin->Geo->DrawArgs["diamond"].BaseVertexLocation;
    // Avoid the skull texture (TexStoneTop) looking like a weird banner.
    // Use a more neutral stone texture for the basin.
    fountainBasin->TexSrvHeapIndex = TexStone;
    mAllRitems.push_back(std::move(fountainBasin));

    // Pedestal (small cylinder)
    auto fountainPedestal = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&fountainPedestal->World,
        // Scaled down by ~2x from the last oversized version.
        XMMatrixScaling(6.375f, 3.375f, 6.375f) *
        XMMatrixTranslation(0.0f, 4.70f, 0.0f));
    fountainPedestal->ObjCBIndex = objCBIndex++;
    fountainPedestal->Geo = mGeometries["shapeGeo"].get();
    fountainPedestal->IndexCount = fountainPedestal->Geo->DrawArgs["cylinderTrunk"].IndexCount;
    fountainPedestal->StartIndexLocation = fountainPedestal->Geo->DrawArgs["cylinderTrunk"].StartIndexLocation;
    fountainPedestal->BaseVertexLocation = fountainPedestal->Geo->DrawArgs["cylinderTrunk"].BaseVertexLocation;
    fountainPedestal->TexSrvHeapIndex = TexStoneNeck;
    mAllRitems.push_back(std::move(fountainPedestal));

    // Main water jet (use cone + cylinder-ish core).
    auto splashCore = std::make_unique<RenderItem>();
    splashCore->Alpha = 0.75f;
    XMStoreFloat4x4(&splashCore->World,
        XMMatrixScaling(3.375f, 6.0f, 3.375f) *
        XMMatrixTranslation(0.0f, 9.70f, 0.0f));
    splashCore->ObjCBIndex = objCBIndex++;
    splashCore->Geo = mGeometries["shapeGeo"].get();
    splashCore->IndexCount = splashCore->Geo->DrawArgs["cone"].IndexCount;
    splashCore->StartIndexLocation = splashCore->Geo->DrawArgs["cone"].StartIndexLocation;
    splashCore->BaseVertexLocation = splashCore->Geo->DrawArgs["cone"].BaseVertexLocation;
    splashCore->TexSrvHeapIndex = TexWater;
    mAllRitems.push_back(std::move(splashCore));

    // Outer splash jets (smaller cones around the top for a "big fountain" feel).
    for (int sj = 0; sj < 4; ++sj) {
        float angle = sj * (XM_2PI / 4.0f);
        float x = 4.125f * cosf(angle);
        float z = 4.125f * sinf(angle);

        auto splashRingJet = std::make_unique<RenderItem>();
        splashRingJet->Alpha = 0.65f;
        XMStoreFloat4x4(&splashRingJet->World,
            XMMatrixScaling(1.65f, 3.375f, 1.65f) *
            XMMatrixTranslation(x, 8.20f, z));
        splashRingJet->ObjCBIndex = objCBIndex++;
        splashRingJet->Geo = mGeometries["shapeGeo"].get();
        splashRingJet->IndexCount = splashRingJet->Geo->DrawArgs["cone"].IndexCount;
        splashRingJet->StartIndexLocation = splashRingJet->Geo->DrawArgs["cone"].StartIndexLocation;
        splashRingJet->BaseVertexLocation = splashRingJet->Geo->DrawArgs["cone"].BaseVertexLocation;
        splashRingJet->TexSrvHeapIndex = TexWater;
        mAllRitems.push_back(std::move(splashRingJet));
    }

    // Additional water core (cylinderTrunk reused as a water column segment).
    auto waterColumn = std::make_unique<RenderItem>();
    waterColumn->Alpha = 0.55f;
    XMStoreFloat4x4(&waterColumn->World,
        XMMatrixScaling(1.35f, 6.375f, 1.35f) *
        XMMatrixTranslation(0.0f, 9.70f, 0.0f));
    waterColumn->ObjCBIndex = objCBIndex++;
    waterColumn->Geo = mGeometries["shapeGeo"].get();
    waterColumn->IndexCount = waterColumn->Geo->DrawArgs["cylinderTrunk"].IndexCount;
    waterColumn->StartIndexLocation = waterColumn->Geo->DrawArgs["cylinderTrunk"].StartIndexLocation;
    waterColumn->BaseVertexLocation = waterColumn->Geo->DrawArgs["cylinderTrunk"].BaseVertexLocation;
    waterColumn->TexSrvHeapIndex = TexWater;
    mAllRitems.push_back(std::move(waterColumn));

    // 5. CORNER TOWERS AND ROOFS (trunk/neck/top use different stone)
    float towerPos[4][2] = { {-12, 12}, {12, 12}, {-12, -12}, {12, -12} };
    for (int i = 0; i < 4; ++i) {
        // Trunk segment
        auto towerTrunk = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&towerTrunk->World,
            XMMatrixScaling(2.5f, 4.0f, 2.5f) *
            XMMatrixTranslation(towerPos[i][0], 6.0f, towerPos[i][1]));
        towerTrunk->ObjCBIndex = objCBIndex++;
        towerTrunk->Geo = mGeometries["shapeGeo"].get();
        towerTrunk->IndexCount = towerTrunk->Geo->DrawArgs["cylinderTower"].IndexCount;
        towerTrunk->StartIndexLocation = towerTrunk->Geo->DrawArgs["cylinderTower"].StartIndexLocation;
        towerTrunk->BaseVertexLocation = towerTrunk->Geo->DrawArgs["cylinderTower"].BaseVertexLocation;
        towerTrunk->TexSrvHeapIndex = TexStone;
        mAllRitems.push_back(std::move(towerTrunk));

        // Neck segment
        auto towerNeck = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&towerNeck->World,
            XMMatrixScaling(2.5f, 1.5f, 2.5f) *
            XMMatrixTranslation(towerPos[i][0], 14.25f, towerPos[i][1]));
        towerNeck->ObjCBIndex = objCBIndex++;
        towerNeck->Geo = mGeometries["shapeGeo"].get();
        towerNeck->IndexCount = towerNeck->Geo->DrawArgs["cylinderTower"].IndexCount;
        towerNeck->StartIndexLocation = towerNeck->Geo->DrawArgs["cylinderTower"].StartIndexLocation;
        towerNeck->BaseVertexLocation = towerNeck->Geo->DrawArgs["cylinderTower"].BaseVertexLocation;
        towerNeck->TexSrvHeapIndex = TexStoneNeck;
        mAllRitems.push_back(std::move(towerNeck));

        // Brick belts (thin cylindrical rings) to add tower detail.
        auto beltLower = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&beltLower->World,
            XMMatrixScaling(2.55f, 0.25f, 2.55f) *
            XMMatrixTranslation(towerPos[i][0], 13.2f, towerPos[i][1]));
        beltLower->ObjCBIndex = objCBIndex++;
        beltLower->Geo = mGeometries["shapeGeo"].get();
        beltLower->IndexCount = beltLower->Geo->DrawArgs["cylinderTower"].IndexCount;
        beltLower->StartIndexLocation = beltLower->Geo->DrawArgs["cylinderTower"].StartIndexLocation;
        beltLower->BaseVertexLocation = beltLower->Geo->DrawArgs["cylinderTower"].BaseVertexLocation;
        beltLower->TexSrvHeapIndex = TexStoneNeck;
        mAllRitems.push_back(std::move(beltLower));

        auto beltUpper = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&beltUpper->World,
            XMMatrixScaling(2.55f, 0.18f, 2.55f) *
            XMMatrixTranslation(towerPos[i][0], 16.9f, towerPos[i][1]));
        beltUpper->ObjCBIndex = objCBIndex++;
        beltUpper->Geo = mGeometries["shapeGeo"].get();
        beltUpper->IndexCount = beltUpper->Geo->DrawArgs["cylinderTower"].IndexCount;
        beltUpper->StartIndexLocation = beltUpper->Geo->DrawArgs["cylinderTower"].StartIndexLocation;
        beltUpper->BaseVertexLocation = beltUpper->Geo->DrawArgs["cylinderTower"].BaseVertexLocation;
        beltUpper->TexSrvHeapIndex = TexStoneTop;
        mAllRitems.push_back(std::move(beltUpper));

        auto roof = std::make_unique<RenderItem>();
        // Tower top: sphere cap.
        XMStoreFloat4x4(&roof->World,
            XMMatrixScaling(4.0f, 4.0f, 4.0f) *
            XMMatrixTranslation(towerPos[i][0], 18.6f, towerPos[i][1]));
        roof->ObjCBIndex = objCBIndex++;
        roof->Geo = mGeometries["shapeGeo"].get();
        roof->IndexCount = roof->Geo->DrawArgs["sphereRoof"].IndexCount;
        roof->StartIndexLocation = roof->Geo->DrawArgs["sphereRoof"].StartIndexLocation;
        roof->BaseVertexLocation = roof->Geo->DrawArgs["sphereRoof"].BaseVertexLocation;
        roof->TexSrvHeapIndex = TexStoneTop;
        mAllRitems.push_back(std::move(roof));

        // Small finials: cone spike and triangular prism accent on the dome.

        auto coneFinial = std::make_unique<RenderItem>();
        XMStoreFloat4x4(
            &coneFinial->World,
            XMMatrixScaling(0.4f, 0.7f, 0.4f) *
            XMMatrixTranslation(towerPos[i][0], 21.4f, towerPos[i][1]));
        coneFinial->ObjCBIndex = objCBIndex++;
        coneFinial->Geo = mGeometries["shapeGeo"].get();
        coneFinial->IndexCount = coneFinial->Geo->DrawArgs["cone"].IndexCount;
        coneFinial->StartIndexLocation = coneFinial->Geo->DrawArgs["cone"].StartIndexLocation;
        coneFinial->BaseVertexLocation = coneFinial->Geo->DrawArgs["cone"].BaseVertexLocation;
        coneFinial->TexSrvHeapIndex = TexStoneTop;
        mAllRitems.push_back(std::move(coneFinial));

        auto triPrismCap = std::make_unique<RenderItem>();
        XMStoreFloat4x4(
            &triPrismCap->World,
            XMMatrixRotationZ(XMConvertToRadians(30.0f)) *
            XMMatrixScaling(0.55f, 0.25f, 0.35f) *
            XMMatrixTranslation(towerPos[i][0], 20.9f, towerPos[i][1]));
        triPrismCap->ObjCBIndex = objCBIndex++;
        triPrismCap->Geo = mGeometries["shapeGeo"].get();
        triPrismCap->IndexCount = triPrismCap->Geo->DrawArgs["triPrism"].IndexCount;
        triPrismCap->StartIndexLocation = triPrismCap->Geo->DrawArgs["triPrism"].StartIndexLocation;
        triPrismCap->BaseVertexLocation = triPrismCap->Geo->DrawArgs["triPrism"].BaseVertexLocation;
        triPrismCap->TexSrvHeapIndex = TexStoneNeck;
        mAllRitems.push_back(std::move(triPrismCap));
    }

    // 5. ENCLOSING WALLS
    // Back Wall (North)
    auto wallN = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wallN->World, XMMatrixScaling(24, 8, 1) * XMMatrixTranslation(0, 4, 12));
    wallN->ObjCBIndex = objCBIndex++;
    wallN->Geo = mGeometries["shapeGeo"].get();
    wallN->IndexCount = wallN->Geo->DrawArgs["stoneBox"].IndexCount;
    wallN->StartIndexLocation = wallN->Geo->DrawArgs["stoneBox"].StartIndexLocation;
    wallN->BaseVertexLocation = wallN->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
    wallN->TexSrvHeapIndex = TexBrickBase;
    mAllRitems.push_back(std::move(wallN));

    // Side Walls (East & West)
    float sideX[2] = { -12, 12 };
    for (int i = 0; i < 2; ++i) {
        auto sideWall = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&sideWall->World, XMMatrixScaling(1, 8, 24) * XMMatrixTranslation(sideX[i], 4, 0));
        sideWall->ObjCBIndex = objCBIndex++;
        sideWall->Geo = mGeometries["shapeGeo"].get();
        sideWall->IndexCount = sideWall->Geo->DrawArgs["stoneBox"].IndexCount;
        sideWall->StartIndexLocation = sideWall->Geo->DrawArgs["stoneBox"].StartIndexLocation;
        sideWall->BaseVertexLocation = sideWall->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
        sideWall->TexSrvHeapIndex = TexBrickBase;
        mAllRitems.push_back(std::move(sideWall));
    }

    // Front Gatehouse Walls (Two walls with entrance gap)
    float gateWallX[2] = { -8.5, 8.5 };
    for (int i = 0; i < 2; ++i) {
        auto gWall = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&gWall->World, XMMatrixScaling(7, 8, 1) * XMMatrixTranslation(gateWallX[i], 4, -12));
        gWall->ObjCBIndex = objCBIndex++;
        gWall->Geo = mGeometries["shapeGeo"].get();
        gWall->IndexCount = gWall->Geo->DrawArgs["stoneBox"].IndexCount;
        gWall->StartIndexLocation = gWall->Geo->DrawArgs["stoneBox"].StartIndexLocation;
        gWall->BaseVertexLocation = gWall->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
        gWall->TexSrvHeapIndex = TexBrickBase;
        mAllRitems.push_back(std::move(gWall));
    }

    // Gate pillars + lintel (inside the entrance gap) and a pyramid keystone.
    float gatePillarX[2] = { -4.0f, 4.0f };
    for (int i = 0; i < 2; ++i) {
        auto pillar = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&pillar->World,
            XMMatrixScaling(1.0f, 8.0f, 1.0f) *
            XMMatrixTranslation(gatePillarX[i], 4.0f, -12.0f));
        pillar->ObjCBIndex = objCBIndex++;
        pillar->Geo = mGeometries["shapeGeo"].get();
        pillar->IndexCount = pillar->Geo->DrawArgs["woodBox"].IndexCount;
        pillar->StartIndexLocation = pillar->Geo->DrawArgs["woodBox"].StartIndexLocation;
        pillar->BaseVertexLocation = pillar->Geo->DrawArgs["woodBox"].BaseVertexLocation;
        pillar->TexSrvHeapIndex = TexWood;
        mAllRitems.push_back(std::move(pillar));
    }

    auto gateLintel = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&gateLintel->World,
        XMMatrixScaling(8.0f, 0.8f, 1.0f) *
        XMMatrixTranslation(0.0f, 7.6f, -12.0f));
    gateLintel->ObjCBIndex = objCBIndex++;
    gateLintel->Geo = mGeometries["shapeGeo"].get();
    gateLintel->IndexCount = gateLintel->Geo->DrawArgs["woodBox"].IndexCount;
    gateLintel->StartIndexLocation = gateLintel->Geo->DrawArgs["woodBox"].StartIndexLocation;
    gateLintel->BaseVertexLocation = gateLintel->Geo->DrawArgs["woodBox"].BaseVertexLocation;
    gateLintel->TexSrvHeapIndex = TexWood;
    mAllRitems.push_back(std::move(gateLintel));

    // Simple wooden gate slab in the entrance gap (so the gate reads as wood, not stone).
    auto gateDoor = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&gateDoor->World,
        XMMatrixScaling(6.4f, 4.2f, 0.25f) *
        XMMatrixTranslation(0.0f, 3.6f, -12.0f));
    gateDoor->ObjCBIndex = objCBIndex++;
    gateDoor->Geo = mGeometries["shapeGeo"].get();
    gateDoor->IndexCount = gateDoor->Geo->DrawArgs["woodBox"].IndexCount;
    gateDoor->StartIndexLocation = gateDoor->Geo->DrawArgs["woodBox"].StartIndexLocation;
    gateDoor->BaseVertexLocation = gateDoor->Geo->DrawArgs["woodBox"].BaseVertexLocation;
    gateDoor->TexSrvHeapIndex = TexWood;
    mAllRitems.push_back(std::move(gateDoor));

    auto gateKeystone = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&gateKeystone->World,
        XMMatrixScaling(2.2f, 2.2f, 2.2f) *
        XMMatrixTranslation(0.0f, 8.0f, -12.0f));
    gateKeystone->ObjCBIndex = objCBIndex++;
    gateKeystone->Geo = mGeometries["shapeGeo"].get();
    gateKeystone->IndexCount = gateKeystone->Geo->DrawArgs["pyramid"].IndexCount;
    gateKeystone->StartIndexLocation = gateKeystone->Geo->DrawArgs["pyramid"].StartIndexLocation;
    gateKeystone->BaseVertexLocation = gateKeystone->Geo->DrawArgs["pyramid"].BaseVertexLocation;
    gateKeystone->TexSrvHeapIndex = TexStone;
    mAllRitems.push_back(std::move(gateKeystone));

    // Hex-style window on each gate wall (uses wedge + diamond shapes).
    // It is symmetric and intentionally small so it doesn't break silhouette.
    for (int gi = 0; gi < 2; ++gi) {
        const float centerX = gateWallX[gi];
        const float centerY = 6.0f;
        const float centerZ = -11.65f; // slightly in front of the gate wall

        // Outer "hex" made from 6 rotated wedges.
        for (int k = 0; k < 6; ++k) {
            float angle = k * (XM_2PI / 6.0f);
            auto windowSeg = std::make_unique<RenderItem>();
            XMStoreFloat4x4(
                &windowSeg->World,
                XMMatrixRotationZ(angle) *
                XMMatrixScaling(0.45f, 0.65f, 0.08f) *
                XMMatrixTranslation(centerX, centerY, centerZ));
            windowSeg->ObjCBIndex = objCBIndex++;
            windowSeg->Geo = mGeometries["shapeGeo"].get();
            windowSeg->IndexCount = windowSeg->Geo->DrawArgs["wedge"].IndexCount;
            windowSeg->StartIndexLocation = windowSeg->Geo->DrawArgs["wedge"].StartIndexLocation;
            windowSeg->BaseVertexLocation = windowSeg->Geo->DrawArgs["wedge"].BaseVertexLocation;
            windowSeg->TexSrvHeapIndex = TexBrickTop;
            mAllRitems.push_back(std::move(windowSeg));
        }

        // Inner diamond medallion.
        auto innerDiamond = std::make_unique<RenderItem>();
        XMStoreFloat4x4(
            &innerDiamond->World,
            XMMatrixRotationZ(XMConvertToRadians(45.0f)) *
            XMMatrixScaling(0.25f, 0.25f, 0.08f) *
            XMMatrixTranslation(centerX, centerY, centerZ));
        innerDiamond->ObjCBIndex = objCBIndex++;
        innerDiamond->Geo = mGeometries["shapeGeo"].get();
        innerDiamond->IndexCount = innerDiamond->Geo->DrawArgs["diamond"].IndexCount;
        innerDiamond->StartIndexLocation = innerDiamond->Geo->DrawArgs["diamond"].StartIndexLocation;
        innerDiamond->BaseVertexLocation = innerDiamond->Geo->DrawArgs["diamond"].BaseVertexLocation;
        innerDiamond->TexSrvHeapIndex = TexBrickBase;
        mAllRitems.push_back(std::move(innerDiamond));
    }

    // Brick detail: horizontal bands + vertical buttresses (reuses wall textures).
    // These are intentionally simple so they still read as brickwork from a distance.

    // North wall bands and buttresses.
    {
        // Horizontal course band (slightly below battlements).
        auto northBand = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&northBand->World,
            XMMatrixScaling(22.0f, 0.4f, 0.25f) *
            XMMatrixTranslation(0.0f, 6.0f, 12.45f));
        northBand->ObjCBIndex = objCBIndex++;
        northBand->Geo = mGeometries["shapeGeo"].get();
        northBand->IndexCount = northBand->Geo->DrawArgs["stoneBox"].IndexCount;
        northBand->StartIndexLocation = northBand->Geo->DrawArgs["stoneBox"].StartIndexLocation;
        northBand->BaseVertexLocation = northBand->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
        northBand->TexSrvHeapIndex = TexBrickTop;
        mAllRitems.push_back(std::move(northBand));

        // A few symmetric protruding brick buttresses.
        float buttX[4] = { -8.0f, -4.0f, 4.0f, 8.0f };
        for (int bi = 0; bi < 4; ++bi) {
            auto butt = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&butt->World,
                XMMatrixScaling(1.0f, 6.0f, 0.35f) *
                XMMatrixTranslation(buttX[bi], 3.0f, 12.45f));
            butt->ObjCBIndex = objCBIndex++;
            butt->Geo = mGeometries["shapeGeo"].get();
            butt->IndexCount = butt->Geo->DrawArgs["stoneBox"].IndexCount;
            butt->StartIndexLocation = butt->Geo->DrawArgs["stoneBox"].StartIndexLocation;
            butt->BaseVertexLocation = butt->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
            butt->TexSrvHeapIndex = TexBrickBase;
            mAllRitems.push_back(std::move(butt));
        }
    }

    // Side walls bands and buttresses (east & west).
    float sideZ[3] = { -8.0f, 0.0f, 8.0f };
    for (int wi = 0; wi < 2; ++wi) {
        float x = (wi == 0) ? -12.0f : 12.0f;
        float outwardX = (wi == 0) ? -12.45f : 12.45f;

        auto sideBand = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&sideBand->World,
            XMMatrixScaling(0.25f, 0.4f, 22.0f) *
            XMMatrixTranslation(outwardX, 6.0f, 0.0f));
        sideBand->ObjCBIndex = objCBIndex++;
        sideBand->Geo = mGeometries["shapeGeo"].get();
        sideBand->IndexCount = sideBand->Geo->DrawArgs["stoneBox"].IndexCount;
        sideBand->StartIndexLocation = sideBand->Geo->DrawArgs["stoneBox"].StartIndexLocation;
        sideBand->BaseVertexLocation = sideBand->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
        sideBand->TexSrvHeapIndex = TexBrickTop;
        mAllRitems.push_back(std::move(sideBand));

        for (int bi = 0; bi < 3; ++bi) {
            auto butt = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&butt->World,
                XMMatrixScaling(0.35f, 6.0f, 1.0f) *
                XMMatrixTranslation(outwardX, 3.0f, sideZ[bi]));
            butt->ObjCBIndex = objCBIndex++;
            butt->Geo = mGeometries["shapeGeo"].get();
            butt->IndexCount = butt->Geo->DrawArgs["stoneBox"].IndexCount;
            butt->StartIndexLocation = butt->Geo->DrawArgs["stoneBox"].StartIndexLocation;
            butt->BaseVertexLocation = butt->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
            butt->TexSrvHeapIndex = TexBrickBase;
            mAllRitems.push_back(std::move(butt));
        }
    }

    // Gate wall bands and buttresses.
    float gateWallDetailX[2] = { -8.5f, 8.5f };
    for (int gi = 0; gi < 2; ++gi) {
        float outwardZ = -12.45f;
        // Horizontal course band across each gate wall piece.
        auto gateBand = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&gateBand->World,
            XMMatrixScaling(6.5f, 0.4f, 0.25f) *
            XMMatrixTranslation(gateWallDetailX[gi], 6.0f, outwardZ));
        gateBand->ObjCBIndex = objCBIndex++;
        gateBand->Geo = mGeometries["shapeGeo"].get();
        gateBand->IndexCount = gateBand->Geo->DrawArgs["stoneBox"].IndexCount;
        gateBand->StartIndexLocation = gateBand->Geo->DrawArgs["stoneBox"].StartIndexLocation;
        gateBand->BaseVertexLocation = gateBand->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
        gateBand->TexSrvHeapIndex = TexBrickTop;
        mAllRitems.push_back(std::move(gateBand));

        // Two symmetric buttresses per gate wall piece.
        float localX[2] = { -2.0f, 2.0f };
        for (int bi = 0; bi < 2; ++bi) {
            auto butt = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&butt->World,
                XMMatrixScaling(0.9f, 6.0f, 0.35f) *
                XMMatrixTranslation(gateWallDetailX[gi] + localX[bi], 3.0f, outwardZ));
            butt->ObjCBIndex = objCBIndex++;
            butt->Geo = mGeometries["shapeGeo"].get();
            butt->IndexCount = butt->Geo->DrawArgs["stoneBox"].IndexCount;
            butt->StartIndexLocation = butt->Geo->DrawArgs["stoneBox"].StartIndexLocation;
            butt->BaseVertexLocation = butt->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
            butt->TexSrvHeapIndex = TexBrickBase;
            mAllRitems.push_back(std::move(butt));
        }
    }

    // Battlements along the top edges of the walls.
    for (int i = -5; i <= 5; ++i) {
        float x = i * 2.0f;
        auto tooth = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&tooth->World,
            XMMatrixScaling(1.5f, 1.0f, 0.8f) *
            XMMatrixTranslation(x, 8.5f, 12.0f));
        tooth->ObjCBIndex = objCBIndex++;
        tooth->Geo = mGeometries["shapeGeo"].get();
        tooth->IndexCount = tooth->Geo->DrawArgs["stoneBox"].IndexCount;
        tooth->StartIndexLocation = tooth->Geo->DrawArgs["stoneBox"].StartIndexLocation;
        tooth->BaseVertexLocation = tooth->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
        tooth->TexSrvHeapIndex = TexBrickTop;
        mAllRitems.push_back(std::move(tooth));
    }
    for (int i = -5; i <= 5; ++i) {
        float z = i * 2.0f;
        for (int side = 0; side < 2; ++side) {
            float x = (side == 0) ? -12.0f : 12.0f;
            auto tooth = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&tooth->World,
                XMMatrixScaling(0.8f, 1.0f, 1.5f) *
                XMMatrixTranslation(x, 8.5f, z));
            tooth->ObjCBIndex = objCBIndex++;
            tooth->Geo = mGeometries["shapeGeo"].get();
            tooth->IndexCount = tooth->Geo->DrawArgs["stoneBox"].IndexCount;
            tooth->StartIndexLocation = tooth->Geo->DrawArgs["stoneBox"].StartIndexLocation;
            tooth->BaseVertexLocation = tooth->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
            tooth->TexSrvHeapIndex = TexBrickTop;
            mAllRitems.push_back(std::move(tooth));
        }
    }
    float frontTeethX[6] = { -10.0f, -8.0f, -6.0f, 6.0f, 8.0f, 10.0f };
    for (int i = 0; i < 6; ++i) {
        float x = frontTeethX[i];
        auto tooth = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&tooth->World,
            XMMatrixScaling(1.5f, 1.0f, 0.8f) *
            XMMatrixTranslation(x, 8.5f, -12.0f));
        tooth->ObjCBIndex = objCBIndex++;
        tooth->Geo = mGeometries["shapeGeo"].get();
        tooth->IndexCount = tooth->Geo->DrawArgs["stoneBox"].IndexCount;
        tooth->StartIndexLocation = tooth->Geo->DrawArgs["stoneBox"].StartIndexLocation;
        tooth->BaseVertexLocation = tooth->Geo->DrawArgs["stoneBox"].BaseVertexLocation;
        tooth->TexSrvHeapIndex = TexBrickTop;
        mAllRitems.push_back(std::move(tooth));
    }

    // Trees: rings around world origin; any sample inside the maze footprint is pushed outward (billboards stay off the maze).
    {
        using namespace MazeConfig;
        const float treeRadiusInner = 34.0f;
        const float treeInset = 3.5f;
        const float R = outerBound - treeInset;
        const float treeGroundY = kTreeBillboardHalfH;
        const float mazePad = kTreeBillboardHalfW + 2.0f;
        const float mazeMinX = kOriginX - mazePad;
        const float mazeMaxX = kOriginX + static_cast<float>(kCols) * kCell + mazePad;
        const float mazeMinZ = kOriginZ - mazePad;
        const float mazeMaxZ =
            kOriginZ + static_cast<float>(kRows) * kCell + mazePad + kLastRowZOffsetTowardCastle;

        auto pushOutsideMazeFootprint = [&](float px, float pz) {
            if (px > mazeMinX && px < mazeMaxX && pz > mazeMinZ && pz < mazeMaxZ) {
                float len0 = sqrtf(px * px + pz * pz);
                if (len0 < 1e-3f) {
                    px = 1.0f;
                    pz = 0.0f;
                    len0 = 1.0f;
                }
                const float ux = px / len0;
                const float uz = pz / len0;
                float t = len0;
                for (int s = 0; s < 160; ++s) {
                    const float qx = t * ux;
                    const float qz = t * uz;
                    if (qx <= mazeMinX || qx >= mazeMaxX || qz <= mazeMinZ || qz >= mazeMaxZ)
                        return XMFLOAT2(qx, qz);
                    t += 0.55f;
                }
                return XMFLOAT2(ux * (outerBound - 1.0f), uz * (outerBound - 1.0f));
            }
            return XMFLOAT2(px, pz);
        };

        int ti = 0;
        for (int i = 0; i < kBillboardOriginalRingTrees; ++i) {
            const float angle = static_cast<float>(i) * (XM_2PI / static_cast<float>(kBillboardOriginalRingTrees));
            float x = treeRadiusInner * cosf(angle);
            float z = treeRadiusInner * sinf(angle);
            XMFLOAT2 xz = pushOutsideMazeFootprint(x, z);
            x = xz.x;
            z = xz.y;
            auto tree = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&tree->World, XMMatrixTranslation(x, treeGroundY, z));
            tree->BillboardTree = true;
            tree->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            tree->ObjCBIndex = objCBIndex++;
            tree->Geo = mGeometries["treeBillboardGeo"].get();
            tree->IndexCount = 1;
            tree->StartIndexLocation = (UINT)ti;
            tree->BaseVertexLocation = ti++;
            tree->TexSrvHeapIndex = TexTreeLeaves;
            mAllRitems.push_back(std::move(tree));
        }
        for (int i = 0; i < kBillboardOuterRingTrees; ++i) {
            const float angle =
                (static_cast<float>(i) + 0.5f) * (XM_2PI / static_cast<float>(kBillboardOuterRingTrees));
            float x = R * cosf(angle);
            float z = R * sinf(angle);
            XMFLOAT2 xz = pushOutsideMazeFootprint(x, z);
            x = xz.x;
            z = xz.y;
            auto tree = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&tree->World, XMMatrixTranslation(x, treeGroundY, z));
            tree->BillboardTree = true;
            tree->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            tree->ObjCBIndex = objCBIndex++;
            tree->Geo = mGeometries["treeBillboardGeo"].get();
            tree->IndexCount = 1;
            tree->StartIndexLocation = (UINT)ti;
            tree->BaseVertexLocation = ti++;
            tree->TexSrvHeapIndex = TexTreeLeaves;
            mAllRitems.push_back(std::move(tree));
        }
        static_assert(
            kBillboardOriginalRingTrees + kBillboardOuterRingTrees == kBillboardTreeCount,
            "tree budget");
    }

    // 5. THE DRAWBRIDGE
    // Main bridge slab.
    auto bridge = std::make_unique<RenderItem>();
    // Lift + open over the moat so you can see the water channel clearly.
    XMMATRIX bridgeWorld =
        XMMatrixScaling(10.0f, 0.4f, 13.0f) *
        // Lean upwards towards the outer (front) side.
        XMMatrixRotationX(XMConvertToRadians(35.0f)) *
        // Raise the bridge so it clears the moat and looks like it's lifted.
        // Raise it an extra ~half panel height so it doesn't intersect the water.
        XMMatrixTranslation(0.0f, 3.25f, -18.5f);
    XMStoreFloat4x4(&bridge->World, bridgeWorld);
    bridge->ObjCBIndex = objCBIndex++;
    bridge->Geo = mGeometries["shapeGeo"].get();
    bridge->IndexCount = bridge->Geo->DrawArgs["woodBox"].IndexCount;
    bridge->StartIndexLocation = bridge->Geo->DrawArgs["woodBox"].StartIndexLocation;
    bridge->BaseVertexLocation = bridge->Geo->DrawArgs["woodBox"].BaseVertexLocation;
    bridge->TexSrvHeapIndex = TexWoodBridge;
    mAllRitems.push_back(std::move(bridge));

    // Symmetric side rails (simple extra geometry for more realism).
    float railX = 4.75f; // half of the bridge width minus a small margin
    for (int side = 0; side < 2; ++side) {
        float x = (side == 0) ? -railX : railX;
        auto rail = std::make_unique<RenderItem>();
        XMMATRIX railWorld =
            XMMatrixScaling(0.25f, 0.6f, 13.0f) *
            XMMatrixRotationX(XMConvertToRadians(35.0f)) *
            XMMatrixTranslation(x, 3.60f, -18.5f);
        XMStoreFloat4x4(&rail->World, railWorld);
        rail->ObjCBIndex = objCBIndex++;
        rail->Geo = mGeometries["shapeGeo"].get();
        rail->IndexCount = rail->Geo->DrawArgs["woodBox"].IndexCount;
        rail->StartIndexLocation = rail->Geo->DrawArgs["woodBox"].StartIndexLocation;
        rail->BaseVertexLocation = rail->Geo->DrawArgs["woodBox"].BaseVertexLocation;
        rail->TexSrvHeapIndex = TexWoodBridge;
        mAllRitems.push_back(std::move(rail));
    }

    // A small hinge/beam block at the wall side of the bridge.
    auto hinge = std::make_unique<RenderItem>();
    XMStoreFloat4x4(
        &hinge->World,
        XMMatrixScaling(0.9f, 0.4f, 0.9f) *
        XMMatrixTranslation(0.0f, 3.15f, -17.8f));
    hinge->ObjCBIndex = objCBIndex++;
    hinge->Geo = mGeometries["shapeGeo"].get();
    hinge->IndexCount = hinge->Geo->DrawArgs["woodBox"].IndexCount;
    hinge->StartIndexLocation = hinge->Geo->DrawArgs["woodBox"].StartIndexLocation;
    hinge->BaseVertexLocation = hinge->Geo->DrawArgs["woodBox"].BaseVertexLocation;
    hinge->TexSrvHeapIndex = TexWoodBridge;
    mAllRitems.push_back(std::move(hinge));

    // Draw order lists: TexWater uses transparent PSO; trees use tree PSO; rest opaque.
    for (auto& e : mAllRitems) {
        if (e->TexSrvHeapIndex == TexWater)
            mTransparentRitems.push_back(e.get());
        else if (e->BillboardTree)
            mBillboardTreeRitems.push_back(e.get());
        else
            mOpaqueRitems.push_back(e.get());
    }
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    for (size_t i = 0; i < ritems.size(); ++i) {
        auto ri = ritems[i];
        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);
        UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mAllRitems.size() + ri->ObjCBIndex;
        auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
        cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);
        cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

        auto texHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
        texHandle.Offset(mTextureSrvBaseIndex + ri->TexSrvHeapIndex, mCbvSrvUavDescriptorSize);
        cmdList->SetGraphicsRootDescriptorTable(2, texHandle);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void ShapesApp::DrawBillboardTrees(ID3D12GraphicsCommandList* cmdList)
{
    if (mBillboardTreeRitems.empty())
        return;
    if (mIsWireframe)
        cmdList->SetPipelineState(mPSOs["tree_billboard_wireframe"].Get());
    else
        cmdList->SetPipelineState(mPSOs["tree_billboard"].Get());
    DrawRenderItems(cmdList, mBillboardTreeRitems);
}