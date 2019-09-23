//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "RayTracingHlslCompat.h"

namespace ComputeShader {   // ToDo remove this?
	namespace Type {
		enum Enum {
			CompositionCS,
			Count
		};
	}

	namespace RootSignature {
		namespace CompositionCS {
			namespace Slot {
				enum Enum {
					Output = 0,
					GBufferResources,
					AO,
					MaterialBuffer,
					ConstantBuffer,
                    Variance,
                    LocalMeanVariance,
                    AORayHitDistance,
                    Tspp,   // ToDo use same name as in the shader
                    Color,
                    AOSurfaceAlbedo,
                    Count
				};
			}
		}		
	}
	namespace RS = RootSignature;
}
namespace CSType = ComputeShader::Type;
namespace CSRootSignature = ComputeShader::RootSignature;

// ToDo move?
namespace RayGenShaderType {
    enum Enum {
        Pathtracer = 0,
        Count
    };
}

namespace GBufferResource {
	enum Enum {
		Hit = 0,		// Geometry hit or not.
        // ToDo rename to AORay hit members?
		Material,		// Material of the object hit ~ {MaterialID, texCoord}.
		HitPosition,	// 3D position of hit.
		SurfaceNormalDepth,	// Encoded normal.
        Depth,          // Linear depth of the hit.
        PartialDepthDerivatives,
        MotionVector,
        ReprojectedNormalDepth,
        Color,
        AOSurfaceAlbedo,
		Count
	};
}

namespace AOResource {
	enum Enum {
		AmbientCoefficient = 0, 
        Smoothed,   // ToDo remove
        RayHitDistance,
		Count
	};
}

namespace AOVarianceResource {
    enum Enum {
        Raw = 0,
        Smoothed,
        Count
    };
}

namespace TemporalSupersampling {
    enum Enum {
        Tspp = 0,
        RayHitDistance,
        CoefficientSquaredMean,
        Count
    };
}

namespace SampleScene {
	namespace Type {
		enum Enum {
			Main,
			Count
		};
		extern const WCHAR* Names[Count];
	}

	struct Camera
	{
		struct CameraPosition
		{
			XMVECTOR eye, at, up;
		};

		struct CameraBoundaries
		{
			XMVECTOR min, max;
		};

		CameraPosition position;
		CameraBoundaries boundaries;
	};

	struct Params {
		Camera camera;
	};

	class Initialize
	{
	public:
		Initialize();
	};
	extern Params args[SampleScene::Type::Count];
}

namespace SceneEnums
{
	namespace VertexBuffer {
		enum Value { SceneGeometry = 0, Count };
	}
}
