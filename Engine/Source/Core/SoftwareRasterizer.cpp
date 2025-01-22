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
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include <limits>

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
	DepthData = new float[inImageWidth * inImageHeight];

	ClearImageBuffers();
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

inline glm::vec4 TransformPosition(const glm::vec3& inVtx, const glm::mat4& inMat)
{
	glm::vec4 vtxTransformed = inMat * glm::vec4(inVtx.x, inVtx.y, inVtx.z, 1.f);

	return vtxTransformed;
}

inline glm::vec3 HomDivide(const glm::vec4& inHomCoordinates)
{
	glm::vec3 out = glm::vec3(inHomCoordinates);
	const float hommCoordinate = inHomCoordinates.w;
	// Persp divide
	// Can give divide by 0 when vtx is on near plane as dist is 0 then
	if (hommCoordinate != 0)
	{
		out /= hommCoordinate;
	}

	return out;
}

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

	const Transform& modelTrans = inModel->GetAbsoluteTransform();
	const glm::mat4 absoluteMat = modelTrans.GetMatrix();
	//const glm::mat4 projection = glm::orthoLH_ZO(-20.f, 20.f, -20.f, 20.f, 0.f, 20.f);
	const glm::mat4 projection = glm::perspectiveLH_ZO(glm::radians(CAMERA_FOV), static_cast<float>(ImageWidth) / static_cast<float>(ImageHeight), CAMERA_NEAR, CAMERA_FAR);
	// Object needs to be at + or - 120 to be drawn, why?

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

					const glm::vec3 currVtx = CPUVertices[currIndex].Position;
					const glm::vec3 nextVtx = CPUVertices[nextIndex].Position;

					glm::vec3 currTranfsVtx = HomDivide(TransformPosition(currVtx, projection * absoluteMat));
					glm::vec3 nextTranfsVtx = HomDivide(TransformPosition(nextVtx, projection * absoluteMat));

					// Re-map from -1..1 to 0..1
					const glm::vec3 remappedCurrVtx = (currTranfsVtx + 1.f) / 2.f;
					const glm::vec3 remappedNextVtx = (nextTranfsVtx + 1.f) / 2.f;

					// TODO: Barycentric coordinates to get interpolate between vertex z and write to z buffer(to use for culling) and also remove if behind camera

					// Re-map to pixel space
					const glm::vec2i start(remappedCurrVtx.x * (ImageWidth - 1), remappedCurrVtx.y * (ImageHeight - 1));
					const glm::vec2i end(remappedNextVtx.x * (ImageWidth - 1), remappedNextVtx.y * (ImageHeight - 1));

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
		int32_t pixelPos = 0;
		if (TryGetPixelPos(x, y, pixelPos))
		{
			FinalImageData[pixelPos] = ConvertToRGBA(inColor);
		}

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

int32_t maxTriangles = 128;
bool bDrawTriangleWireframe = false;
bool bDrawOnlyBackfaceCulled = false;
bool bUseZBuffer = true;

void SoftwareRasterizer::BeginFrame()
{
	// ImGui
	{
		ImGui::Begin("Software Rasterizer");
		ImGui::SliderInt("Triangles to draw", &maxTriangles, 0, 32);
		ImGui::Checkbox("Draw Triangle Wireframe", &bDrawTriangleWireframe);
		ImGui::Checkbox("Draw Only Backface culled", &bDrawOnlyBackfaceCulled);
		ImGui::Checkbox("Use Z-Buffer", &bUseZBuffer);
		ImGui::End();
	}

	ClearImageBuffers();
}

void SoftwareRasterizer::ClearImageBuffers()
{
	memset(FinalImageData, 0, ImageWidth * ImageHeight * 4);
	// Workaround for windef macro causing compilation issues: https://stackoverflow.com/questions/1394132/macro-and-member-function-conflict
	constexpr float maxDepth = (std::numeric_limits<float>::max)();
	for (int32_t i = 0; i < ImageWidth * ImageHeight; ++i)
	{
		DepthData[i] = maxDepth;
	}
}

bool SoftwareRasterizer::TryGetPixelPos(const int32_t X, const int32_t Y, int32_t& outPixelPos)
{
	outPixelPos = Y * ImageWidth + X;
	const bool bValidPixel = X >= 0 && Y >= 0 && X < ImageWidth && Y < ImageHeight && outPixelPos >= 0 && (outPixelPos < (ImageWidth * ImageHeight));

	return bValidPixel;
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

void SoftwareRasterizer::DrawModel(const eastl::shared_ptr<Model3D>& inModel)
{


	int32_t countTriangles = 0;

	const eastl::vector<TransformObjPtr>& modelChildren = inModel->GetChildren();

	const Transform& modelTrans = inModel->GetAbsoluteTransform();
	const glm::mat4 absoluteMat = modelTrans.GetMatrix();
	const float orthoAABBHalfLength = 5.f;
	//const glm::mat4 projection = glm::orthoLH_ZO(-orthoAABBHalfLength, orthoAABBHalfLength, -orthoAABBHalfLength, orthoAABBHalfLength, 0.f, orthoAABBHalfLength * 2);
	const glm::mat4 projection = glm::perspectiveLH_ZO(glm::radians(CAMERA_FOV), static_cast<float>(ImageWidth) / static_cast<float>(ImageHeight), CAMERA_NEAR, CAMERA_FAR);

	//const glm::mat4 perspTest = glm::perspectiveLH_ZO(glm::radians(CAMERA_FOV), static_cast<float>(ImageWidth) / static_cast<float>(ImageHeight), 1.f, 3.f);
	//const glm::vec4 t1 = glm::vec4(0.f, 0.f, 2.f, 1.f);
	//const glm::vec4 t2 = perspTest * t1;
	//const glm::vec4 t3 = t2 / t2.w;

	SceneManager& sManager = SceneManager::Get();
	const Scene& currentScene = sManager.GetCurrentScene();
	const glm::mat4 view = currentScene.GetMainCameraLookAt();

	const eastl::vector<MeshMaterial>& materials = inModel->Materials;
	for (uint32_t i = 0; i < modelChildren.size(); ++i)
	{
		const TransformObjPtr& currChild = modelChildren[i];
		eastl::shared_ptr<MeshNode> node = eastl::dynamic_shared_pointer_cast<MeshNode>(currChild);
		if (node)
		{
			const MeshMaterial& currMaterial = materials[node->MatIndex];
			const eastl::shared_ptr<D3D12Texture2D>& currTex = currMaterial.AlbedoMap;
			const DirectX::ScratchImage& dxImage = currTex->CPUImage;

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

				// Vtx Shader
				// Outputs clip space
				{
					const SimpleVertex& vtxA = CPUVertices[CPUIndices[idxStart]];
					const SimpleVertex& vtxB = CPUVertices[CPUIndices[idxStart + 1]];
					const SimpleVertex& vtxC = CPUVertices[CPUIndices[idxStart + 2]];

					const glm::vec3 vtxAPos = vtxA.Position;
					const glm::vec3 vtxBPos = vtxB.Position;
					const glm::vec3 vtxCPos = vtxC.Position;

					const glm::mat4 worldToClip = projection * view * absoluteMat;
					//const glm::mat4 worldToClip = glm::mat4(1.f);

					const glm::vec4 vtxAClipsSpace = TransformPosition(vtxAPos, worldToClip);
					const glm::vec4 vtxBClipSpace = TransformPosition(vtxBPos, worldToClip);
					const glm::vec4 vtxCClipSpace = TransformPosition(vtxCPos, worldToClip);

					//const glm::vec3 viewDir = currentScene.GetCurrentCamera()->GetViewDir();
					const glm::vec3 viewDir = view[2]; // Z axis of rotation

					DrawTriangle({ vtxAClipsSpace, vtxA.Normal, vtxA.TexCoords }, { vtxBClipSpace, vtxB.Normal, vtxB.TexCoords }, { vtxCClipSpace, vtxC.Normal, vtxC.TexCoords }, dxImage.GetImages()[0]);
				}
				++countTriangles;

			}
		}

	}
}

void SoftwareRasterizer::DrawTriangle(const VtxShaderOutput& A, const VtxShaderOutput& B, const VtxShaderOutput& C, const DirectX::Image& CPUImage)
{
	const glm::vec3 A_NDC = HomDivide(A.ClipSpacePos);
	const glm::vec3 B_NDC = HomDivide(B.ClipSpacePos);
	const glm::vec3 C_NDC = HomDivide(C.ClipSpacePos);

	// Clip based on clip space coords defined by projection matrix
	// TODO: This should split the triangle up when it is only partially in the clip space
	//if (A_NDC.z < 0.f || A_NDC.z > 1.f || B_NDC.z < 0.f || B_NDC.z > 1.f || C_NDC.z < 0.f || C_NDC.z > 1.f)
	//{
	//	return;
	//}

	bool bCulled = false;
	// Backface cull the triangle
	//{
	//	const glm::vec3 v0 = A_NDC;
	//	const glm::vec3 v1 = B_NDC;
	//	const glm::vec3 v2 = C_NDC;

	//	const glm::vec3 v0v1 = v1 - v0;
	//	const glm::vec3 v0v2 = v2 - v0;

	//	// LH CCW culling
	//	const glm::vec3 trianglePlaneNormal = glm::normalize(glm::cross(v0v1, v0v2));

	//	// CW
	//	//const glm::vec3 trianglePlaneNormal = glm::normalize(glm::cross(v0v2, v0v1));
	//	
	//	//LOG_INFO("Triangle normal is %.2f, %.2f, %.2f", trianglePlaneNormal.x, trianglePlaneNormal.y, trianglePlaneNormal.z);

	//	const float dot = glm::dot(trianglePlaneNormal, glm::vec3(0.f, 0.f, 1.f));
	//	// dot for dir from cam to vertex with triangle normal, dir cam to vert is v0 - camLoc, camLoc is origin (0,0,0) in viewSpace, so just v0
	//	//const float dot = glm::dot(trianglePlaneNormal, v0);

	//	//LOG_INFO("Dot product is %.2f", dot);

	//	// Triangle is visible only when normal is pointing towards camera, so opposite of camera dir
	//	if (dot >= 0.f)
	//	{
	//		// Skip triangle
	//		bCulled = true;
	//		if (!bDrawOnlyBackfaceCulled)
	//		{
	//			return;
	//		}
	//	}
	//}

	// Map from -1..1 to 0..1 (Screen Space)
	const glm::vec3 vtxAScreenSpace = (A_NDC + 1.f) / 2.f;
	const glm::vec3 vtxBScreenSpace = (B_NDC + 1.f) / 2.f;
	const glm::vec3 vtxCScreenSpace = (C_NDC + 1.f) / 2.f;

	// Map to pixel space
	const glm::vec2 A_PS(vtxAScreenSpace.x * (ImageWidth - 1), vtxAScreenSpace.y * (ImageHeight - 1));
	const glm::vec2 B_PS(vtxBScreenSpace.x * (ImageWidth - 1), vtxBScreenSpace.y * (ImageHeight - 1));
	const glm::vec2 C_PS(vtxCScreenSpace.x * (ImageWidth - 1), vtxCScreenSpace.y * (ImageHeight - 1));

	if (bDrawTriangleWireframe)
	{
		DrawLine(A_PS, B_PS, glm::vec4(0.f, 1.f, 0.f, 1.f));
		DrawLine(B_PS, C_PS, glm::vec4(1.f, 0.f, 0.f, 1.f));
		DrawLine(C_PS, A_PS, glm::vec4(0.f, 0.f, 1.f, 1.f));
	}


	// Bounding Box
	// draw inside of it and check if pixel is in using cross product method but for 2d vectors
	// might also be possible to check using barycentric coordinates, need to check

	AABB2D box;
	box += A_PS;
	box += B_PS;
	box += C_PS;

	const glm::vec2& min = box.Min;
	const glm::vec2& max = box.Max;

	const glm::vec2 AB = B_PS - A_PS;
	const glm::vec2 BC = C_PS - B_PS;
	const glm::vec2 CA = A_PS - C_PS;

	const int32_t pixelMinY = static_cast<int32_t>(min.y);
	const int32_t pixelMinX = static_cast<int32_t>(min.x);
	const int32_t pixelMaxY = static_cast<int32_t>(max.y);
	const int32_t pixelMaxX = static_cast<int32_t>(max.x);

	for (int32_t i = pixelMinY; i <= pixelMaxY; ++i)
	{
		for (int32_t j = pixelMinX; j <= pixelMaxX; ++j)
		{

			int32_t pixelPos = 0;
			if (!TryGetPixelPos(j, i, pixelPos))
			{
				continue;
			}

			const glm::vec2 P(j + 0.5f, i + 0.5f); // Move P to pixel center

			// Derive barycentric coordinates and from those determine if pixel is in triangle
			// Formula in Drive document at Barycentric Coordiantes section

			float wA, wB, wC;
			// Cramer's rule for 2D vertices barycentric coordinates
			{
				const glm::vec2 V0 = B_PS - A_PS;
				const glm::vec2 V1 = C_PS - A_PS;
				const glm::vec2 V2 = P - A_PS;

				const float det = V0.x * V1.y - V1.x * V0.y;

				// Calculate 1/det to replace 2 divisons with 1 division and 2 mult
				const float oneOverDet = det != 0.f ? 1.f / det : 1.f; // Det is 0 when vtx is on near plane

				wB = (V2.x * V1.y - V1.x * V2.y) * oneOverDet;
				wC = (V0.x * V2.y - V2.x * V0.y) * oneOverDet;
				wA = 1.f - wB - wC;
			}

			if (wA < 0.f || wB < 0.f || wC < 0.f || wA > 1.f || wB >1.f || wC > 1.f)
			{
				continue;
			}

			// x, y, z can be linearly interpolated in screen space using screen space derived barycentrics.
			// However, nothing that's in camera space can be derived using just the screen space derived barycentrics
			// For that we need the camera space z.
			
			const float ndcDepth = (wA * A_NDC.z) + (wB * B_NDC.z) + (wC * C_NDC.z);
			const float pixelCameraSpaceDepth = 1.f / ((wA / A.ClipSpacePos.w) + (wB / B.ClipSpacePos.w) + (wC / C.ClipSpacePos.w)); // Depth in camera space, 
			// we need this because this for everything else because this is what gets used to do the perspective divide

			
			// Divide texcoords by z, to "transform them to post perspective divide space"
			// Then, multiply by the new z to get back the standard space value.
			// Could also be explained as "x * 1/z is linear across the screen space interpolation)
			glm::vec2 texCoordsPerspInterp = wA * (A.TexCoords / A.ClipSpacePos.w) + wB * (B.TexCoords / B.ClipSpacePos.w) + wC *(C.TexCoords / C.ClipSpacePos.w);
			texCoordsPerspInterp *= pixelCameraSpaceDepth;

			texCoordsPerspInterp.y = 1.f - texCoordsPerspInterp.y;

			// To get any other coordinate of the current pixel, we can just linearly interpolate it in NDC space and multiply by current pixel Camera Space depth
			// to get camera space values.
			// Eg.
			//const float NDCcurrentPixelX = (wA * A_NDC.z) + (wB * B_NDC.z) + (wC * C_NDC.z);
			//const float cameraSpacePixelX = NDCcurrentPixelX * pixelCameraSpaceDepth;

			// Z can also be derived and used to test that the operations are correct
			//// Test that demonstrates liniarity in lerping across NDC values
			//// Lerp to get NDC z of current pixel, then transform to Camera Space using derived pixel depth
			//const float m22 = CAMERA_FAR / (CAMERA_FAR - CAMERA_NEAR); // Persp matrix m22
			//const float m23 = -1.f * ((CAMERA_FAR * CAMERA_NEAR) / (CAMERA_FAR - CAMERA_NEAR)); // Persp matrix m23
			//const float ndcDepth = wA * A_NDC.z + wB * B_NDC.z + wC * C_NDC.z;
			//const float CameraDepthAfterPerspOps = ndcDepth * pixelCameraSpaceDepth;
			//const float CameraDepth = (CameraDepthAfterPerspOps - m23) / m22; // Under Persp matrix re-map
			//// CameraDepth == pixelCameraSpaceDepth

			if (ndcDepth <= 0.f || ndcDepth> 1.f)
			{
				continue;
			}

			// Z Buffer
			if (bUseZBuffer)
			{
				const float existingDepth = DepthData[pixelPos];
				if (ndcDepth < existingDepth)
				{
					DepthData[pixelPos] = ndcDepth;
				}
				else
				{
					continue;
				}
			}

			const size_t textureHeight = CPUImage.height;
			const size_t textureWidth = CPUImage.width;
			const uint8_t* texels = CPUImage.pixels;

			const size_t texelX = texCoordsPerspInterp.x * textureWidth;
			const size_t texelY = texCoordsPerspInterp.y * textureHeight;

			const size_t texelPos = texelY * (textureWidth * 4) + (texelX * 4);
			if (texelPos >= CPUImage.slicePitch)
			{
				LOG_WARNING("Tried to sample beyond texture bounds");
				continue;
			}

			const glm::vec3 AColor(A.TexCoords.x, A.TexCoords.y, 0.f);
			const glm::vec3 BColor(B.TexCoords.x, B.TexCoords.y, 0.f);
			const glm::vec3 CColor(C.TexCoords.x, C.TexCoords.y, 0.f);

			uint32_t RGBA = 0;
			uint8_t* bytes = (uint8_t*)&RGBA;
			bytes[0] = texels[texelPos];
			bytes[1] = texels[texelPos + 1];
			bytes[2] = texels[texelPos + 2];
			bytes[3] = texels[texelPos + 3];

			const glm::vec3 UVColor = glm::vec3(texCoordsPerspInterp.x, texCoordsPerspInterp.y, 0.f);

			if (bDrawOnlyBackfaceCulled)
			{
				if (bCulled)
				{
					FinalImageData[pixelPos] = ConvertToRGBA(glm::vec4(1.f, 0.f, 0.f, 1.f));
				}
			}
			else
			{
				//FinalImageData[pixelPos] = ConvertToRGBA(glm::vec4(UVColor.x, UVColor.y, UVColor.z, 1.f));
				FinalImageData[pixelPos] = RGBA;
			}
		}
	}


}

void SoftwareRasterizer::DrawPoint(const glm::vec2i& inPoint, const glm::vec4& inColor)
{
	int32_t pixelPos = 0;
	if (TryGetPixelPos(inPoint.x, inPoint.y, pixelPos))
	{
		FinalImageData[pixelPos] = ConvertToRGBA(inColor);
	}
}

void SoftwareRasterizer::DoTest()
{
	{
		const glm::vec2 A(40, 58);
		const glm::vec2 B(10,10);
		const glm::vec2 C(70,30);

		//DrawPoint(B);

		//DrawTriangle(VtxShaderOutput{ A, 0.f, glm::vec3(0.f, 0.f, 0.f), glm::vec2(0.f, 0.f) }, VtxShaderOutput{ B, 0.f, glm::vec3(0.f, 0.f, 0.f), glm::vec2(0.f, 0.f) }, VtxShaderOutput{ C, 0.f, glm::vec3(0.f, 0.f, 0.f), glm::vec2(0.f, 0.f) });

		//DrawLine(A, B, glm::vec4(0.f, 1.f, 0.f, 1.f));
		//DrawLine(B, C, glm::vec4(0.f, 1.f, 0.f, 1.f));
		//DrawLine(C, A, glm::vec4(0.f, 1.f, 0.f, 1.f));


		//DrawLine(glm::vec2i(10, -5), glm::vec2i(10, 10), glm::vec4(0.f, 1.f, 0.f, 1.f));
		//DrawLine(glm::vec2i(10, -20), glm::vec2i(10, 50), glm::vec4(0.f, 1.f, 0.f, 1.f));
	}

}
