#pragma once

#include "Core/EngineUtils.h"
#include "EASTL/array.h"
#include "EASTL/vector.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float2.hpp"

struct AABB2Di
{
	glm::vec2i Min;
	glm::vec2i Max;

	AABB2Di& operator +=(const AABB2Di& inAABB);
	AABB2Di& operator +=(const glm::vec2i& inVec);

	inline glm::vec2i GetExtent() const
	{
		return (Max - Min) / 2;
	}

	inline void GetCenterAndExtent(OUT glm::vec2i& outCenter, OUT glm::vec2i& outExtent) const
	{
		outExtent = GetExtent();
		outCenter = Min + outExtent;
	}

	//eastl::array<glm::vec3, 8> GetVertices() const;
	//void DebugDraw() const;

private:
	bool IsInitialized = false;
};

struct AABB2D
{
	glm::vec2 Min;
	glm::vec2 Max;

	AABB2D& operator +=(const AABB2D& inAABB);
	AABB2D& operator +=(const glm::vec2& inVec);

	inline glm::vec2 GetExtent() const
	{
		return (Max - Min) / 2.f;
	}

	inline void GetCenterAndExtent(OUT glm::vec2& outCenter, OUT glm::vec2& outExtent) const
	{
		outExtent = GetExtent();
		outCenter = Min + outExtent;
	}

	//eastl::array<glm::vec3, 8> GetVertices() const;
	//void DebugDraw() const;

private:
	bool IsInitialized = false;
};


struct AABB
{
	glm::vec3 Min;
	glm::vec3 Max;

	AABB& operator +=(const AABB& inAABB);
	AABB& operator +=(const glm::vec3& inVec);


	inline glm::vec3 GetExtent() const
	{
		return 0.5f * (Max - Min);
	}

	inline void GetCenterAndExtent(OUT glm::vec3& outCenter, OUT glm::vec3& outExtent) const
	{
		outExtent = GetExtent();
		outCenter = Min + outExtent;
	}

	eastl::array<glm::vec3, 8> GetVertices() const;

	void DebugDraw() const;

private:
	bool IsInitialized = false;
};