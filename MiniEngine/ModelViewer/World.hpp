
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

		template<typename ActionT >
		void ForEach(ActionT&& action)
		{
			for (auto& model : m_models) 
			{
				action(model);
			}
		}

	private:

		void CaculateBoundingBox();

		

		BoundingBox m_boundingbox;
	};
}
