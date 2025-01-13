#pragma once
#include <stdint.h>
#include "glm/glm.hpp"
#include "glm/ext/vector_float2.hpp"
#include "EASTL/shared_ptr.h"

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
	inline void ClearImage();
	inline bool TryGetPixelPos(const int32_t X, const int32_t Y, int32_t& outPixelPos);

	void DrawTriangle(const glm::vec2i& A, const glm::vec2i& B, const glm::vec2i& C);
	void DrawPoint(const glm::vec2i& inPoint, const glm::vec4& inColor = glm::vec4(1.f, 1.f, 1.f, 1.f));
	void DoTest();

private:
	uint32_t* FinalImageData = nullptr;
	glm::vec4* IntermediaryImageData = nullptr;
	int32_t ImageWidth = 0;
	int32_t ImageHeight = 0;
};
