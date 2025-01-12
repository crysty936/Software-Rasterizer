#include "Core/SoftwareRasterizer.h"
#include "Logger/Logger.h"
#include "EngineUtils.h"

#include <algorithm>
#include <execution>
#include <random>
#include "Entity/TransformObject.h"
#include "Renderer/Model/3D/Model3D.h"
#include "imgui.h"
#include "Math/AABB.h"

static uint32_t ConvertToRGBA(const glm::vec4& color)
{
	uint8_t r = (uint8_t)(color.r * 255.0f);
	uint8_t g = (uint8_t)(color.g * 255.0f);
	uint8_t b = (uint8_t)(color.b * 255.0f);
	uint8_t a = (uint8_t)(color.a * 255.0f);

	uint32_t result = (a << 24) | (b << 16) | (g << 8) | r;
	return result;
}

inline float random_float() {
	static std::uniform_real_distribution<float> distribution(0.0, 1.0);
	static std::mt19937 generator;
	return distribution(generator);
}

inline float random_float(float min, float max) {
	// Returns a random real in [min,max).
	return min + (max - min) * random_float();
}

static glm::vec3 randomVec3() {
	return glm::vec3(random_float(), random_float(), random_float());
}

void SoftwareRasterizer::Init(const int32_t inImageWidth, const int32_t inImageHeight)
{
	ImageWidth = inImageWidth;
	ImageHeight = inImageHeight;

	IntermediaryImageData = new glm::vec4[inImageWidth * inImageHeight];
	FinalImageData = new uint32_t[inImageWidth * inImageHeight];
	ClearImage();
}

SoftwareRasterizer::~SoftwareRasterizer()
{
	delete IntermediaryImageData;
	delete FinalImageData;
}

void SoftwareRasterizer::TransposeImage()
{
	for (int32_t i = 0; i < ImageHeight/2; ++i)
	{
		char* swap1 = (char*)&FinalImageData[i * ImageWidth];
		char* swap1End = (char*)&FinalImageData[i * ImageWidth + ImageWidth];// End of line
		//--swap1End;// Go back 1 character to end of line

		char* swap2 = (char*)&FinalImageData[(ImageHeight - 1 - i) * ImageWidth];

		std::swap_ranges((char*)swap1, swap1End, swap2);
	}
}

//void SoftwareRasterizer::DrawLine(const glm::vec2i& inStart, const glm::vec2i& inEnd, const glm::vec4& inColor)
//{
//	const int32_t dx = glm::abs(inEnd.x - inStart.x);
//	const int32_t dy = glm::abs(inEnd.y - inStart.y);
//
//	const int32_t nrSteps = dx > dy ? dx : dy;
//
//	glm::vec2i start = inStart.x < inEnd.x ? inStart : inEnd;
//	glm::vec2i end = inStart.x < inEnd.x ? inEnd : inStart;
//
//	const float slopeY = float(dy) / float(dx);
//	const float slopeX = float(dx) / float(dy);
//	float slopeErrorY = 0.f;
//	float slopeErrorX = 0.f;
//
//	int32_t x = start.x;
//	int32_t y = start.y;
//
//	for (int32_t i = 0; i <= nrSteps; ++i)
//	{
//		FinalImageData[y * ImageWidth + x] = 0xFFFFFFFF;
//		slopeErrorY += slopeY;
//		slopeErrorX += slopeX;
//
//		if (slopeErrorY > 0.5f)
//		{
//			++y;
//			slopeErrorY -= 1.f; // Normally, we would increase the stepped over pixel by 1
//								// and compare with the next one but instead of that, we can decrease this by 1
//		}
//
//		if (slopeErrorX > 0.5f)
//		{
//			++x;
//			slopeErrorX -= 1.f;
//		}
//
//	}
//}

const float CAMERA_FOV = 45.f;
const float CAMERA_NEAR = 0.1f;
const float CAMERA_FAR = 100.f;



void SoftwareRasterizer::DrawModelWireframe(const eastl::shared_ptr<Model3D>& inModel)
{
	static int32_t maxLines = 128;
	static int32_t maxTriangles = 32;

	{
		ImGui::Begin("Software Rasterizer");
		ImGui::SliderInt("Lines to draw", &maxLines, 0, 128);
		ImGui::SliderInt("Triangles to draw", &maxTriangles, 0, 32);
		ImGui::End();
	}

	int32_t countLines = 0;
	int32_t countTriangles = 0;

	const eastl::vector<TransformObjPtr>& modelChildren = inModel->GetChildren();

#define USE_PROJECTION 0

#if USE_PROJECTION
	const Transform& modelTrans = inModel->GetAbsoluteTransform();
	const glm::mat4 absoluteMat = modelTrans.GetMatrix();
	const glm::mat4 projection = glm::orthoLH_ZO(-20.f, 20.f, -20.f, 20.f, 0.f, 20.f);
	//const glm::mat4 projection = glm::perspectiveLH_ZO(glm::radians(CAMERA_FOV), static_cast<float>(ImageWidth) / static_cast<float>(ImageHeight), CAMERA_NEAR, CAMERA_FAR);
	// Object needs to be at + or - 120 to be drawn, why?
#endif


	for (uint32_t i = 0; i < modelChildren.size(); ++i)
	{
		const TransformObjPtr& currChild = modelChildren[i];
		eastl::shared_ptr<MeshNode> node = eastl::dynamic_shared_pointer_cast<MeshNode>(currChild);
		if (node)
		{
			const eastl::vector<SimpleVertex> CPUVertices = node->CPUVertices;
			const eastl::vector<uint32_t> CPUIndices = node->CPUIndices;

			const uint32_t numIndices = static_cast<uint32_t>(CPUIndices.size());
			ASSERT(numIndices % 3 == 0);
			const uint32_t numTriangles = numIndices / 3;

			// Draw triangle by triangle
			for (uint32_t triangleIdx = 0; triangleIdx < numTriangles; ++triangleIdx)
			{
				if (countTriangles >= maxTriangles)
				{
					return;
				}

				const uint32_t idxStart = triangleIdx * 3;

				// Backface cull
				{
					const glm::vec3 v0 = CPUVertices[CPUIndices[idxStart]].Position;
					const glm::vec3 v1 = CPUVertices[CPUIndices[idxStart + 1]].Position;
					const glm::vec3 v2 = CPUVertices[CPUIndices[idxStart + 2]].Position;

					const glm::vec3 v0v1 = v1 - v0;
					const glm::vec3 v0v2 = v2 - v0;

					// LH CCW culling
					const glm::vec3 trianglePlaneNormal = glm::normalize(glm::cross(v0v1, v0v2));
					const glm::vec3 viewDir = glm::vec3(0.f, 0.f, 1.f);

					const float dot = glm::dot(trianglePlaneNormal, viewDir);

					if (dot < 0.f)
					{
						// Skip triangle
						continue;
					}
				}

				for (uint32_t j = 0; j < 3; ++j)
				{
					if (countLines >= maxLines)
					{
						return;
					}
					const uint32_t currIndex = CPUIndices[idxStart + j];
					const uint32_t nextIndexIndex = j == 2 ? idxStart : (idxStart + j + 1);
					const uint32_t nextIndex = CPUIndices[nextIndexIndex];

					glm::vec3 currVertex = CPUVertices[currIndex].Position;
					glm::vec3 nextVertex = CPUVertices[nextIndex].Position;

#if USE_PROJECTION
					// Current vertex
					{
						glm::vec4 currTransformedVertex = absoluteMat * glm::vec4(currVertex.x, currVertex.y, currVertex.z, 1.f);
						currTransformedVertex = currTransformedVertex * projection;
						currTransformedVertex /= currTransformedVertex.w;
						currVertex = glm::vec3(currTransformedVertex);
					}

					// Next vertex
					{
						glm::vec4 nextTransformedVertex = absoluteMat * glm::vec4(nextVertex.x, nextVertex.y, nextVertex.z, 1.f);
						nextTransformedVertex = nextTransformedVertex * projection;
						nextTransformedVertex /= nextTransformedVertex.w;
						nextVertex = glm::vec3(nextTransformedVertex);
					}
#endif

					// Re-map from -1..1 to 0..1
					currVertex = (currVertex + 1.f) / 2.f;
					nextVertex = (nextVertex + 1.f) / 2.f;

					// Re-map to pixel space
					const glm::vec2i start(currVertex.x * (ImageWidth - 1), currVertex.y * (ImageHeight - 1));
					const glm::vec2i end(nextVertex.x * (ImageWidth - 1), nextVertex.y * (ImageHeight - 1));

					DrawLine(start, end);
					++countLines;
				}

				++countTriangles;
			}
		}

	}

}

void SoftwareRasterizer::DrawLine(const glm::vec2i& inStart, const glm::vec2i& inEnd, const glm::vec4& inColor)
{
	const int32_t dx = glm::abs(inEnd.x - inStart.x);
	const int32_t dy = glm::abs(inEnd.y - inStart.y);

	if (dx < 0 || dx >= ImageWidth || dy < 0 || dy >= ImageHeight)
	{
		return;
	}
	//ASSERT(dx < ImageWidth && dy < ImageHeight);

	const int32_t nrSteps = dx > dy ? dx : dy;

	const int32_t slopeY = dy;
	const int32_t slopeX = dx;
	int32_t slopeErrorY = 0;
	int32_t slopeErrorX = 0;

	const int32_t sx = inStart.x <= inEnd.x ? 1 : -1;
	const int32_t sy = inStart.y <= inEnd.y ? 1 : -1;
	int32_t x = inStart.x;
	int32_t y = inStart.y;

	for (int32_t i = 0; i <= nrSteps; ++i)
	{
		const int32_t pixelPos = y * ImageWidth + x;
		if (pixelPos < 0 || pixelPos > (ImageWidth * ImageHeight))
		{
			continue;
		}

		FinalImageData[pixelPos] = ConvertToRGBA(inColor);
		//LOG_INFO("Writing to x: %d and y: %d", x, y);

		slopeErrorY += slopeY;
		slopeErrorX += slopeX;

		if (2 * slopeErrorY > dx)
		{
			y += sy;
			slopeErrorY -= dx;
		}

		if (2 * slopeErrorX > dy)
		{
			x += sx;
			slopeErrorX -= dy;
		}
	}
}

void SoftwareRasterizer::DrawRandom()
{
	const glm::vec4 ColorRed = glm::vec4(1.f, 0.f, 0.f, 1.f);
	const glm::vec4 ColorBlue = glm::vec4(0.f, 0.f, 1.f, 1.f);

	const float stepSize = float(ImageHeight) / 5;
	for (int32_t i = 0; i < ImageHeight; ++i)
	{
		//const bool bIsRed = (i / int32_t(stepSize)) % 2 == 0;

		for (int32_t j = 0; j < ImageWidth; ++j)
		{
			//const bool bIsRed = (i+j) % 2 == 0;

			//const bool bIsRed = i < 200 && j < 200;
			glm::vec4& currentPixel = IntermediaryImageData[i * ImageWidth + j];
			glm::vec3 random = randomVec3();
			currentPixel.x = random.x;
			currentPixel.y = random.y;
			currentPixel.z = random.z;
			currentPixel.a = 1;
			//currentPixel = bIsRed ? ColorRed : ColorBlue;


			FinalImageData[i * ImageWidth + j] = ConvertToRGBA(currentPixel);
		}
	}
}

uint32_t* SoftwareRasterizer::GetImage()
{
	return FinalImageData;
}

void SoftwareRasterizer::PrepareBeforePresent()
{
	// y goes down in D3D
	TransposeImage();
}

void SoftwareRasterizer::ClearImage()
{
	memset(FinalImageData, 0, ImageWidth * ImageHeight * 4);
}

inline int32_t Get2DCrossProductMagnitude(const glm::vec2i& A, const glm::vec2i& B)
{
	// |a.x b.x| or |a.x a.y|
	// |a.y b.y|    |b.x b.y|
	// determinant of M == det of transpose of M

	// = ax * by - ay * bx

	const int32_t det = A.x * B.y - A.y * B.x;

	return det;
}

void SoftwareRasterizer::DrawTriangle(const glm::vec2i& A, const glm::vec2i& B, const glm::vec2i& C)
{
	// bounding box
	// draw inside of it and check if pixel is in using cross product method but for 2d vectors
	// might also be possible to check using barycentric coordinates, need to check

	AABB2Di box;
	box += A;
	box += B;
	box += C;

	const glm::vec2i& min = box.Min;
	const glm::vec2i& max = box.Max;
	//const int32_t length = max.x - min.x;

	const glm::vec2i AB = B - A;
	const glm::vec2i BC = C - B;
	const glm::vec2i CA = A - C;

	for (int32_t i = min.y; i <= max.y; ++i)
	{
		for (int32_t j = min.x; j <= max.x; ++j)
		{
			const glm::vec2i P(j, i);

			// Check if inside triangle by checking cross product between edges and vectors made from edge origin to P
			// Inside out test
			const glm::vec2i AP = P - A;
			if (Get2DCrossProductMagnitude(AB, AP) < 0)
			{
				continue;
			}
			const glm::vec2i BP = P - B;
			if (Get2DCrossProductMagnitude(BC, BP) < 0)
			{
				continue;
			}

			const glm::vec2i CP = P - C;
			if (Get2DCrossProductMagnitude(CA, CP) < 0)
			{
				continue;
			}

			FinalImageData[i * ImageWidth + j] = ConvertToRGBA(glm::vec4(1.f, 1.f, 1.f, 1.f));
		}
	}


}

void SoftwareRasterizer::DrawPoint(const glm::vec2i& inPoint, const glm::vec4& inColor)
{
	FinalImageData[inPoint.y * ImageWidth + inPoint.x] = ConvertToRGBA(inColor);
}

void SoftwareRasterizer::DoTest()
{
	{
		const glm::vec2i A(40, 58);
		const glm::vec2i B(10,10);
		const glm::vec2i C(70,25);

		//DrawPoint(B);

		DrawTriangle(A, B, C);

		DrawLine(A, B, glm::vec4(0.f, 1.f, 0.f, 1.f));
		DrawLine(B, C, glm::vec4(0.f, 1.f, 0.f, 1.f));
		DrawLine(C, A, glm::vec4(0.f, 1.f, 0.f, 1.f));
	}

}
