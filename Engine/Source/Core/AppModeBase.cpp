#include "Core/AppModeBase.h"
#include "EngineUtils.h"
#include "Renderer/RHI/D3D12/D3D12RHI.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "Renderer/RHI/D3D12/D3D12Utility.h"
#include "Window/WindowsWindow.h"
#include "AppCore.h"
#include "Utils/IOUtils.h"
#include "Renderer/Drawable/ShapesUtils/BasicShapesData.h"
#include "backends/imgui_impl_dx12.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/glm.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Camera/Camera.h"
#include "Utils/Utils.h"
#include "Utils/PerfUtils.h"
#include "Renderer/Model/3D/Assimp/AssimpModel3D.h"
#include "Math/MathUtils.h"
#include "glm/gtc/type_ptr.inl"
#include "SoftwareRasterizer.h"


// Windows includes
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <D3Dcompiler.h>
#define _XM_NO_INTRINSICS_
#include <DirectXMath.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

AppModeBase* AppModeBase::GameMode = new AppModeBase();

AppModeBase::AppModeBase()
{
	ASSERT(!GameMode);

	GameMode = this;
}

#define FRAME_BUFFERING 1

// Synchronization objects.
D3D12Fence m_fence;
D3D12Fence m_FlushGPUFence;

UINT64 m_FlushGPUFenceValue = 0;

uint64_t CurrentCPUFrame = 0;
uint64_t CurrentGPUFrame = 0;

AppModeBase::~AppModeBase()
{
	FlushGPU();
}

// Pipeline objects.

eastl::shared_ptr<D3D12IndexBuffer> ScreenQuadIndexBuffer = nullptr;
eastl::shared_ptr<D3D12VertexBuffer> ScreenQuadVertexBuffer = nullptr;

ID3D12Resource* m_BackBuffers[D3D12Utility::NumFramesInFlight];

ID3D12CommandAllocator* m_commandAllocators[D3D12Utility::NumFramesInFlight];

ID3D12GraphicsCommandList* m_commandList;

ID3D12RootSignature* m_QuadDrawRootSignature;

ID3D12PipelineState* m_QuadDrawPSO;

void AppModeBase::Init()
{
	BENCH_SCOPE("App Mode Init");

	D3D12RHI::Init();

	ImGuiInit();

	CreateInitialResources();
}

glm::mat4 MainProjection;

inline const glm::mat4& GetMainProjection()
{
	return MainProjection;
}

// For main projection
const float CAMERA_FOV = 45.f;
const float CAMERA_NEAR = 0.1f;
const float CAMERA_FAR = 10000.f;

const int32_t SoftRasterizerImgWidth = 640;
const int32_t SoftRasterizerImgHeight = 480;
SoftwareRasterizer Rasterizer;
eastl::shared_ptr<D3D12Texture2DWritable> MainImage;
eastl::shared_ptr<Model3D> MainModel;

void AppModeBase::CreateInitialResources()
{
	BENCH_SCOPE("Create Resources");

	const WindowsWindow& mainWindow = GEngine->GetMainWindow();
	const WindowProperties& props = mainWindow.GetProperties();

	MainProjection = glm::perspectiveLH_ZO(glm::radians(CAMERA_FOV), static_cast<float>(props.Width) / static_cast<float>(props.Height), CAMERA_NEAR, CAMERA_FAR);

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		constexpr uint32_t numRTVs = 32;
		D3D12Globals::GlobalRTVHeap.Init(false, numRTVs, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		constexpr uint32_t numSRVs = 1024;
		constexpr uint32_t numTempSRVs = 128;
		D3D12Globals::GlobalSRVHeap.Init(true, numSRVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, numTempSRVs);

		constexpr uint32_t numDSVs = 32;
		D3D12Globals::GlobalDSVHeap.Init(false, numDSVs, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		constexpr uint32_t numUAVs = 128;
		D3D12Globals::GlobalUAVHeap.Init(false, numUAVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// Create frame resources.
	{
		// Create a RTV for each frame.
		for (UINT i = 0; i < D3D12Utility::NumFramesInFlight; i++)
		{
			// Allocate descriptor space
			D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = D3D12Globals::GlobalRTVHeap.AllocatePersistent().CPUHandle[0];

			// Get a reference to the swapchain buffer
			DXAssert(D3D12Globals::SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i])));

			// Create the descriptor at the target location in the heap
			D3D12Globals::Device->CreateRenderTargetView(m_BackBuffers[i], nullptr, cpuHandle);
			eastl::wstring rtName = L"BackBuffer RenderTarget ";
			rtName += eastl::to_wstring(i);

			m_BackBuffers[i]->SetName(rtName.c_str());

			// Create the command allocator
			DXAssert(D3D12Globals::Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i])));

			eastl::wstring commandAllocatorName = L"CommandAllocator ";
			commandAllocatorName += eastl::to_wstring(i);

			m_commandAllocators[i]->SetName(commandAllocatorName.c_str());
		}
	}

	CreateRootSignatures();

	// Prepare Data

	CreatePSOs();

	// Create the command list.
	DXAssert(D3D12Globals::Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[D3D12Utility::CurrentFrameIndex], m_QuadDrawPSO, IID_PPV_ARGS(&m_commandList)));
	m_commandList->SetName(L"Main GFX Cmd List");


	{
		Rasterizer.Init(SoftRasterizerImgWidth, SoftRasterizerImgHeight);
		MainImage = eastl::make_shared<D3D12Texture2DWritable>(SoftRasterizerImgWidth, SoftRasterizerImgHeight, /*inSRGB*/ false, m_commandList);
	}


	// Create screen quad data
	ScreenQuadIndexBuffer = D3D12RHI::Get()->CreateIndexBuffer(BasicShapesData::GetQuadIndices(), BasicShapesData::GetQuadIndicesCount());

	// Create the vertex buffer
	{
		VertexInputLayout vbLayout;
		vbLayout.Push<float>(3, VertexInputType::Position);
		vbLayout.Push<float>(2, VertexInputType::TexCoords);

		ScreenQuadVertexBuffer = D3D12RHI::Get()->CreateVertexBuffer(vbLayout, BasicShapesData::GetQuadVertices(), BasicShapesData::GetQuadVerticesCount(), ScreenQuadIndexBuffer);
	}

	SceneManager& sManager = SceneManager::Get();
	Scene& currentScene = sManager.GetCurrentScene();

	// Models

	//MainModel = eastl::make_shared<CubeShape>("TheCube");
	MainModel = eastl::make_shared<AssimpModel3D>("../Data/Models/Shiba/scene.gltf", "Model");
	//MainModel = eastl::make_shared<SquareShape>("TheSquare");
	MainModel->Init(m_commandList);
	//MainModel->SetScale(glm::vec3(5.f, 5.f, 5.f));
	//MainModel->SetScale(glm::vec3(1.f, 1.f, 0.5f));
	MainModel->SetRelativeLocation(glm::vec3(0.f, 0.f, 2.f));
	MainModel->Rotate(180.f, glm::vec3(0.f, 1.f, 0.f));

	currentScene.AddObject(MainModel);

	//eastl::shared_ptr<AssimpModel3D> model= eastl::make_shared<AssimpModel3D>("../Data/Models/Sponza/Sponza.gltf", "Sponza");
	//model->Rotate(90.f, glm::vec3(0.f, 1.f, 0.f));
	//model->Move(glm::vec3(0.f, -1.f, -5.f));
	//model->Init(m_commandList);

	//eastl::shared_ptr<AssimpModel3D> model= eastl::make_shared<AssimpModel3D>("../Data/Models/Sphere/scene.gltf", "Sphere");
	//model->Init(m_commandList);

	//eastl::shared_ptr<AssimpModel3D> model= eastl::make_shared<AssimpModel3D>("../Data/Models/Shiba/scene.gltf", "Shiba");
	//model->Init(m_commandList);

	//currentScene.AddObject(model);


	//currentScene.GetCurrentCamera()->Move(EMovementDirection::Back, 10.f);


	DXAssert(m_commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { m_commandList };
	D3D12Globals::GraphicsCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		CurrentCPUFrame = 0;

		m_fence.Init(CurrentCPUFrame);
		++CurrentCPUFrame;

		m_FlushGPUFence.Init(0);
		++m_FlushGPUFenceValue;

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		FlushGPU();

		// Make sure commandlist is open for object creations
		ResetFrameResources();
	}
}


void AppModeBase::CreateRootSignatures()
{
	// Root signature
	{
		D3D12_ROOT_PARAMETER1 rootParameters[1] = {};

		// Textures
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_DESCRIPTOR_RANGE1 texturesRange[1];
		texturesRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		texturesRange[0].BaseShaderRegister = 0;
		texturesRange[0].RegisterSpace = 0;
		texturesRange[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		texturesRange[0].OffsetInDescriptorsFromTableStart = 0;

		texturesRange[0].NumDescriptors = 1;

		rootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(texturesRange);
		rootParameters[0].DescriptorTable.pDescriptorRanges = &texturesRange[0];

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		// Allow input layout and deny uneccessary access to certain pipeline stages.
		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
			| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
			| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
			| D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
		//| D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		rootSignatureDesc.Desc_1_1.NumParameters = _countof(rootParameters);
		rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
		rootSignatureDesc.Desc_1_1.NumStaticSamplers = 1;
		rootSignatureDesc.Desc_1_1.pStaticSamplers = &sampler;
		rootSignatureDesc.Desc_1_1.Flags = rootSignatureFlags;

		m_QuadDrawRootSignature = D3D12RHI::Get()->CreateRootSignature(rootSignatureDesc);
	}
}

void AppModeBase::CreatePSOs()
{
	// Lighting Quad PSO
	{
		eastl::string fullPath = "../Data/Shaders/D3D12/"; ;
		fullPath += "ScreenSizeTexturedQuad.hlsl";

		CompiledShaderResult meshShaderPair = D3D12RHI::Get()->CompileGraphicsShaderFromFile(fullPath);

		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// shader bytecodes
		D3D12_SHADER_BYTECODE vsByteCode;
		vsByteCode.pShaderBytecode = meshShaderPair.VSByteCode->GetBufferPointer();
		vsByteCode.BytecodeLength = meshShaderPair.VSByteCode->GetBufferSize();

		D3D12_SHADER_BYTECODE psByteCode;
		psByteCode.pShaderBytecode = meshShaderPair.PSByteCode->GetBufferPointer();
		psByteCode.BytecodeLength = meshShaderPair.PSByteCode->GetBufferSize();

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_QuadDrawRootSignature;
		psoDesc.VS = vsByteCode;
		psoDesc.PS = psByteCode;
		psoDesc.RasterizerState = D3D12Utility::GetRasterizerState(ERasterizerState::Disabled);
		psoDesc.BlendState = D3D12Utility::GetBlendState(EBlendState::Disabled);
		psoDesc.DepthStencilState = D3D12Utility::GetDepthState(EDepthState::Disabled);
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;

		DXAssert(D3D12Globals::Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_QuadDrawPSO)));
	}
}


void AppModeBase::SwapBuffers()
{
	D3D12Utility::TransitionResource(m_commandList, m_BackBuffers[D3D12Utility::CurrentFrameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	DXAssert(m_commandList->Close());

	bCmdListOpen = false;

	ID3D12CommandList* commandLists[] = { m_commandList };
	D3D12Globals::GraphicsCommandQueue->ExecuteCommandLists(1, commandLists);

	if (GEngine->IsImguiEnabled() && ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault(nullptr, (void*)m_commandList);
	}

	D3D12Globals::SwapChain->Present(1, 0);

	D3D12Utility::CurrentFrameIndex = D3D12Globals::SwapChain->GetCurrentBackBufferIndex();

	++CurrentCPUFrame;

#if FRAME_BUFFERING
	MoveToNextFrame();
#else
	FlushGPU();
#endif

}

void AppModeBase::ResetFrameResources()
{
	if (bCmdListOpen)
	{
		return;
	}

	bCmdListOpen = true;

	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	DXAssert(m_commandAllocators[D3D12Utility::CurrentFrameIndex]->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	DXAssert(m_commandList->Reset(m_commandAllocators[D3D12Utility::CurrentFrameIndex], nullptr));
}

void AppModeBase::BeginFrame()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	//D3D12RHI::Get()->BeginFrame();
	ResetFrameResources();

	// The descriptor heap needs to be set at least once per frame and preferrably never changed during that frame
	// as a change in descriptor heaps can incur a pipeline flush
	// Only one CBV SRV UAV heap and one Samplers heap can be bound at the same time
	ID3D12DescriptorHeap* ppHeaps[] = { D3D12Globals::GlobalSRVHeap.Heaps[D3D12Utility::CurrentFrameIndex] };
	m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);


	{
		Rasterizer.BeginFrame();

		Rasterizer.DrawModel(MainModel);

		//Rasterizer.DrawLine(glm::vec2(40, 30), glm::vec2(0, 30));

		Rasterizer.PrepareBeforePresent();
		uint32_t* imageData = Rasterizer.GetImage();
		D3D12RHI::Get()->UpdateTexture2D(MainImage->GetCurrentImage(), imageData, SoftRasterizerImgWidth, SoftRasterizerImgHeight, m_commandList);
	}


	// Set viewport and scissor region
	{
		const WindowsWindow& mainWindow = GEngine->GetMainWindow();
		const WindowProperties& props = mainWindow.GetProperties();

		static D3D12_VIEWPORT m_viewport;
		m_viewport.Width = static_cast<float>(props.Width);
		m_viewport.Height = static_cast<float>(props.Height);
		m_viewport.MinDepth = 0.f;
		m_viewport.MaxDepth = 1.f;

		m_commandList->RSSetViewports(1, &m_viewport);

		D3D12_RECT scissorRect;
		scissorRect.left = 0;
		scissorRect.top = 0;
		scissorRect.right = props.Width;
		scissorRect.bottom = props.Height;

		m_commandList->RSSetScissorRects(1, &scissorRect);
	}
}

void AppModeBase::RenderTexture()
{
	PIXMarker Marker(m_commandList, "Render Quad");

	// Draw screen quad

	// Backbuffers are the first 2 RTVs in the Global Heap
	D3D12_CPU_DESCRIPTOR_HANDLE currentBackbufferRTDescriptor = D3D12Globals::GlobalRTVHeap.GetCPUHandle(D3D12Utility::CurrentFrameIndex, 0);
	//m_commandList->ClearRenderTargetView(currentBackbufferRTDescriptor, D3D12Utility::ClearColor, 0, nullptr);

	m_commandList->SetGraphicsRootSignature(m_QuadDrawRootSignature);
	m_commandList->SetPipelineState(m_QuadDrawPSO);

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[1];
	renderTargets[0] = currentBackbufferRTDescriptor;

	m_commandList->OMSetRenderTargets(1, renderTargets, false, nullptr);

	m_commandList->SetGraphicsRootDescriptorTable(0, D3D12Globals::GlobalSRVHeap.GetGPUHandle(MainImage->GetCurrentImage()->SRVIndex, D3D12Utility::CurrentFrameIndex));

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_commandList->IASetVertexBuffers(0, 1, &ScreenQuadVertexBuffer->VBView());
	m_commandList->IASetIndexBuffer(&ScreenQuadIndexBuffer->IBView());

	m_commandList->DrawIndexedInstanced(ScreenQuadIndexBuffer->IndexCount, 1, 0, 0, 0);
}

void AppModeBase::Draw()
{
	D3D12Utility::TransitionResource(m_commandList, m_BackBuffers[D3D12Utility::CurrentFrameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	{
		D3D12Utility::TransitionResource(m_commandList, MainImage->GetCurrentImage()->Resource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		RenderTexture();
		D3D12Utility::TransitionResource(m_commandList, MainImage->GetCurrentImage()->Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
	}


	// Draw scene hierarchy
	if (GEngine->IsImguiEnabled())
	{
		ImGui::Begin("Scene");

		SceneManager& sManager = SceneManager::Get();
		Scene& currentScene = sManager.GetCurrentScene();
		currentScene.ImGuiDisplaySceneTree();

		ImGui::End();

		ImGui::Begin("D3D12 Settings");
		ImGui::End();
	}



}

void AppModeBase::EndFrame()
{
	//Draw ImGui
	ImGui::EndFrame();
	ImGui::Render();

	ImGuiRenderDrawData();

	D3D12RHI::Get()->EndFrame();

	SwapBuffers();
}


void AppModeBase::Terminate()
{
	D3D12RHI::Terminate();

	ASSERT(GameMode);

	delete GameMode;
	GameMode = nullptr;


}

void AppModeBase::Tick(float inDeltaT)
{

}

void AppModeBase::FlushGPU()
{
	m_FlushGPUFence.Signal(D3D12Globals::GraphicsCommandQueue, m_FlushGPUFenceValue);
	m_FlushGPUFence.Wait(m_FlushGPUFenceValue);

	++m_FlushGPUFenceValue;
}

// Prepare to render the next frame.
void AppModeBase::MoveToNextFrame()
{
	m_fence.Signal(D3D12Globals::GraphicsCommandQueue, CurrentCPUFrame);

	CurrentGPUFrame = m_fence.GetValue();

	const uint64_t gpuLag = CurrentCPUFrame - CurrentGPUFrame;
	if (gpuLag >= D3D12Utility::NumFramesInFlight)
	{
		// Wait for one frame
		m_fence.Wait(CurrentGPUFrame + 1);

		//LOG_WARNING("Had to wait for GPU");
	}
}

static ID3D12DescriptorHeap* m_imguiCbvSrvHeap;

void AppModeBase::ImGuiInit()
{

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	ImGui::StyleColorsDark();

	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	ImGui_ImplWin32_Init(static_cast<HWND>(GEngine->GetMainWindow().GetHandle()));

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	DXAssert(D3D12Globals::Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_imguiCbvSrvHeap)));

	D3D12_CPU_DESCRIPTOR_HANDLE fontSrvCpuHandle = m_imguiCbvSrvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE fontSrvGpuHandle = m_imguiCbvSrvHeap->GetGPUDescriptorHandleForHeapStart();

	DXGI_FORMAT format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;

	bool success = ImGui_ImplDX12_Init(D3D12Globals::Device, D3D12Utility::NumFramesInFlight, format, m_imguiCbvSrvHeap, fontSrvCpuHandle, fontSrvGpuHandle);

	ASSERT(success);

}

void AppModeBase::ImGuiRenderDrawData()
{
	PIXMarker Marker(m_commandList, "Draw ImGui");

	// Set the imgui descriptor heap
	ID3D12DescriptorHeap* imguiHeaps[] = { m_imguiCbvSrvHeap };
	m_commandList->SetDescriptorHeaps(1, imguiHeaps);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList);
}
