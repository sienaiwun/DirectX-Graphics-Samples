#pragma region 
#include "World.hpp"
#pragma endregion

namespace SceneView
{
	World::World()
	:m_models()
	{}

	void World::AddModel(const std::string& filename, const std::size_t index)
	{
		AssimpModel& model = m_models[index];
		ASSERT(model.Load(filename.c_str()), "Failed to load model:" );
		model.PrintInfo();
	}

	void World::Create()
	{
		m_models.resize(3);
		AddModel("Models/box.obj",0);
		AddModel("Models/sphere.obj",1);
		AddModel("Models/capsule.obj", 2);
		CaculateBoundingBox();
	}

	void World::Clear()
	{
		for (auto& model : m_models) {
			model.Clear();
		}
	}

	void World::CaculateBoundingBox()
	{
		for (const auto& model : m_models) {
			m_boundingbox.min = Min(m_boundingbox.min, model.GetBoundingBox().min);
			m_boundingbox.max = Max(m_boundingbox.max, model.GetBoundingBox().max);
		}
	}
}