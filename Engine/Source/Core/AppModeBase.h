#pragma once

class AppModeBase
{
public:
	AppModeBase();
	virtual ~AppModeBase();

public:

	static AppModeBase* Get() { return GameMode; }

	virtual void Init();

	virtual void BeginFrame();
	virtual void Draw();
	virtual void EndFrame();

	static void Terminate();

	// Game Mode Tick is Run after all other objects
	virtual void Tick(float inDeltaT);
	void FlushGPU();
	void MoveToNextFrame();
	void ImGuiInit();
	void ImGuiRenderDrawData();

private:
	void DrawGBuffer();
	void RenderTexture();
	void ComputeTiledBinning();
	void ComputeDecals();
	void CreateInitialResources();
	void CreateRootSignatures();
	void CreatePSOs();

	void SwapBuffers();
	void ResetFrameResources();

private:
	static AppModeBase* GameMode;

	bool bCmdListOpen = false;

};