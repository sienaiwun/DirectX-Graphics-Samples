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
#ifndef RTAO_HLSLI
#define RTAO_HLSLI

namespace RTAO {
    static const float RayHitDistanceOnMiss = 0;
    // Set invalid ambient coefficient value to -2 so as to be lower than the lowest valid value of -1.
    // Temporal pass marks cached ambient coefficient value <0,1> by negating it so that denoiser knows which values are new and which stale.
    static const float InvalidAOCoefficientValue = -2;
    bool HasAORayHitAnyGeometry(in float tHit)
    {
        return tHit != RayHitDistanceOnMiss;
    }
}

#endif // RTAO_HLSLI