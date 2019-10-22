
#pragma once
#include "ModelAssimp.h"
#include "GameCore.h"

namespace SceneView
{
	class World final
	{
	public:

		World();

		void AddModel(const std::string& filename,const std::size_t index);

		void Create();

		void Clear();

		inline const BoundingBox& GetBoundingBox() const noexcept { return m_boundingbox; }

		std::vector<AssimpModel> m_models;

	private:

		void CaculateBoundingBox();

		

		BoundingBox m_boundingbox;
	};
}
