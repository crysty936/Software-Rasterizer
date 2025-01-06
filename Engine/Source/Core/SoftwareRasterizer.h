#pragma once
#include <stdint.h>
#include "glm/glm.hpp"
#include "glm/ext/vector_float2.hpp"

class SoftwareRasterizer
{
public:
	SoftwareRasterizer() = default;
	void Init(const int32_t inImageWidth, const int32_t inImageHeight);
	~SoftwareRasterizer();
	void TransposeImage();
	void DrawLine(const glm::vec2i& inStart, const glm::vec2i& inEnd, const glm::vec4& inColor = glm::vec4(1.f, 1.f, 1.f, 1.f));
	void bresenhamFull(int x1, int y1, int x2, int y2);
	uint32_t* GetImage();

private:
	uint32_t* FinalImageData = nullptr;
	glm::vec4* IntermediaryImageData = nullptr;
	int32_t ImageWidth = 0;
	int32_t ImageHeight = 0;
};
