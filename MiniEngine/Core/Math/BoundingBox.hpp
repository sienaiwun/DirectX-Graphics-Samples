#pragma once
#include "Vector.h"


namespace Math
{
	struct BoundingBox
	{
		Vector3 min;
		Vector3 max;

		BoundingBox(Vector3 p_min, Vector3 p_max) noexcept
			: min(p_min),
			max(p_max) {}

		BoundingBox() noexcept :max(-g_XMInfinity), min(g_XMInfinity)
		{
		}
	};
}
