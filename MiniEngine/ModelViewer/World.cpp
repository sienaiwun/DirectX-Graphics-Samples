#pragma region 
#include "World.hpp"
#pragma endregion

namespace SceneView
{
	World::World()
	:m_models()
	{}

	void World::AddModel(const std::string& filename)
	{
		AssimpModel model;;
		ASSERT(model.Load(filename.c_str()), "Failed to load model:" );
		model.PrintInfo();
		m_models.emplace_back(std::move(model));

	}

	void World::Create()
	{
#if 0
		AddModel("Models/box.obj");
		AddModel("Models/sphere.obj");
		AddModel("Models/capsule.obj");
		AddModel("Models/plane.obj");
#else
		AddModel("Models/sponza.h3d");
#endif
		CaculateBoundingBox();
	}

	void World::Clear()
	{
		ForEach([&](Model& model) {
			model.Clear();
		});
	}

	void World::CaculateBoundingBox()
	{
		ForEach([&](Model& model) {
			m_boundingbox.min = Min(m_boundingbox.min, model.GetBoundingBox().min);
			m_boundingbox.max = Max(m_boundingbox.max, model.GetBoundingBox().max);
		});
	}
}