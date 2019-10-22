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
#if 0
		m_models.resize(4);
		AddModel("Models/box.obj",0);
		AddModel("Models/sphere.obj",1);
		AddModel("Models/capsule.obj", 2);
		AddModel("Models/plane.obj", 2);
#else
		m_models.resize(1);
		AddModel("Models/sponza.h3d", 0);
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