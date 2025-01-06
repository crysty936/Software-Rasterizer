#pragma once
#include "EASTL/string_view.h"

struct WindowProperties
{
	WindowProperties(const int32_t inWidth, const int32_t inHeight)
		:Width(inWidth), Height(inHeight), AspectRatio(float(Width)/Height)
	{}

	eastl::string_view Title = "MainWindow";
	int32_t Width = 0;
	int32_t Height = 0;
	float AspectRatio = 0.f;
	bool VSyncEnabled = false;
};