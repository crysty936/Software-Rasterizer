#pragma once
#include <stdint.h>
#include "glm/glm.hpp"
#include "glm/ext/vector_float2.hpp"
#include "EASTL/shared_ptr.h"
#include "DirectXTex.h"

class SoftwareRasterizer
{
public:
	SoftwareRasterizer() = default;
	void Init(const int32_t inImageWidth, const int32_t inImageHeight);
	~SoftwareRasterizer();
	void TransposeImage();
	void DrawModel(const eastl::shared_ptr<class Model3D>& inModel);
	void DrawModelWireframe(const eastl::shared_ptr<class Model3D>& inModel);
	void DrawLine(const glm::vec2i& inStart, const glm::vec2i& inEnd, const glm::vec4& inColor = glm::vec4(1.f, 1.f, 1.f, 1.f));
	void DrawRandom();
	void bresenhamFull(int x1, int y1, int x2, int y2);
	uint32_t* GetImage();
	void PrepareBeforePresent();
	void BeginFrame();
	void ClearImageBuffers();
	inline bool TryGetPixelPos(const int32_t X, const int32_t Y, int32_t& outPixelPos);

	struct VtxShaderOutput
	{
		glm::vec4 ClipSpacePos;
		glm::vec3 Normal;
		glm::vec2 TexCoords;
	};

	void DrawTriangle(const VtxShaderOutput& A, const VtxShaderOutput& B, const VtxShaderOutput& C, const DirectX::Image& CPUImage);
	void DrawPoint(const glm::vec2i& inPoint, const glm::vec4& inColor = glm::vec4(1.f, 1.f, 1.f, 1.f));
	void DoTest();

private:

	struct PixelShadeDataPkg
	{
		glm::vec3 A_NDC;
		glm::vec3 B_NDC;
		glm::vec3 C_NDC;

		glm::vec3 vtxAScreenSpace;
		glm::vec3 vtxBScreenSpace;
		glm::vec3 vtxCScreenSpace;

		glm::vec2 A_PS;
		glm::vec2 B_PS;
		glm::vec2 C_PS;

		VtxShaderOutput A;
		VtxShaderOutput B;
		VtxShaderOutput C;

		uint8_t* TexPixels;
		size_t TexWidth;
		size_t TexHeight;

		bool bCulled = false;
	};

	void ShadePixel(const int32_t inX, const int32_t inY, const PixelShadeDataPkg& inPixelData);

private:
	uint32_t* FinalImageData = nullptr;
	float* DepthData = nullptr;
	glm::vec4* IntermediaryImageData = nullptr;
	int32_t ImageWidth = 0;
	int32_t ImageHeight = 0;
};
