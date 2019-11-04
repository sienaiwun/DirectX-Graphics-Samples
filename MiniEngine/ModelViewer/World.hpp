
#pragma once
#include "ModelAssimp.h"
#include "GameCore.h"
#include "CameraController.h"
#include "Camera.h"

using namespace Math;
using namespace GameCore;

namespace SceneView
{
	class World final
	{
	public:

		World();

		void AddModel(const std::string& filename);

		void Create();

		void Clear();

		void Update(const float delta);

		inline const BoundingBox& GetBoundingBox() const noexcept { return m_boundingbox; }

		inline const Camera& GetMainCamera() const noexcept { return m_Camera; }

		inline Camera& GetMainCamera() noexcept { return m_Camera; }

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

		Camera m_Camera;

		std::unique_ptr<CameraController> m_CameraController;

		BoundingBox m_boundingbox;
	};
}
