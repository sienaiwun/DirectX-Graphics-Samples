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

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

// Remove /Zpr and use column-major? It might be slightly faster

#define HLSL
#include "RaytracingHlslCompat.h"
#include "RaytracingShaderHelper.hlsli"
#include "RandomNumberGenerator.hlsli"
#include "SSAO/GlobalSharedHlslCompat.h" // ToDo remove
#include "AnalyticalTextures.hlsli"
#include "BxDF.hlsli"
#define HitDistanceOnMiss -1        // ToDo unify with DISTANCE_ON_MISS

// ToDo split to Raytracing for GBUffer and AO?

// ToDo excise non-GBuffer parts out for separate timings? Such as 

// ToDo dedupe code triangle normal calc,..
// ToDo pix doesn't show output for AO pass

//***************************************************************************
//*****------ Shader resources bound via root signatures -------*************
//***************************************************************************

// Scene wide resources.
//  g_* - bound via a global root signature.
//  l_* - bound via a local root signature.
RaytracingAccelerationStructure g_scene : register(t0, space0);
RWTexture2D<float4> g_renderTarget : register(u0);			// ToDo remove

// ToDo prune redundant
// ToDo move this to local ray gen root sig
RWTexture2D<uint> g_rtGBufferCameraRayHits : register(u5);
// {MaterialId, 16b 2D texCoords}
RWTexture2D<uint2> g_rtGBufferMaterialInfo : register(u6);  // 16b {1x Material Id, 3x Diffuse.RGB}. // ToDo compact to 8b?
RWTexture2D<float4> g_rtGBufferPosition : register(u7);
RWTexture2D<NormalDepthTexFormat> g_rtGBufferNormalDepth : register(u8);
RWTexture2D<float> g_rtGBufferDistance : register(u9);

Texture2D<uint> g_texGBufferPositionHits : register(t5); 
Texture2D<uint2> g_texGBufferMaterialInfo : register(t6);     // 16b {1x Material Id, 3x Diffuse.RGB}       // ToDo rename to material like in composerenderpasses
Texture2D<float4> g_texGBufferPositionRT : register(t7);
Texture2D<NormalDepthTexFormat> g_texGBufferNormalDepth : register(t8);
Texture2D<float4> g_texGBufferDistance : register(t9);
TextureCube<float4> g_texEnvironmentMap : register(t12);
Texture2D<float> g_filterWeightSum : register(t13);
Texture2D<uint> g_texInputAOFrameAge : register(t14);
Texture2D<float> g_texShadowMap : register(t21);
Texture2D<float4> g_texAORaysDirectionOriginDepthHit : register(t22);
Texture2D<uint2> g_texAOSourceToSortedRayIndex : register(t23);

// ToDo remove AOcoefficient and use AO hits instead?
//todo remove rt?
RWTexture2D<float> g_rtAOcoefficient : register(u10);
RWTexture2D<uint> g_rtAORayHits : register(u11);
RWTexture2D<float> g_rtVisibilityCoefficient : register(u12);
RWTexture2D<float> g_rtGBufferDepth : register(u13);
RWTexture2D<float> g_rtAORayHitDistance : register(u15);
#if CALCULATE_PARTIAL_DEPTH_DERIVATIVES_IN_RAYGEN
RWTexture2D<float2> g_rtPartialDepthDerivatives : register(u16);
#endif
// ToDo pack motion vector, depth and normal into float4
RWTexture2D<float2> g_rtTextureSpaceMotionVector : register(u17);
RWTexture2D<NormalDepthTexFormat> g_rtReprojectedNormalDepth : register(u18); // ToDo rename
RWTexture2D<float4> g_rtColor : register(u19);
RWTexture2D<float4> g_rtAOSurfaceAlbedo : register(u20);
RWTexture2D<float> g_rtShadowMap : register(u21);
RWTexture2D<float4> g_rtDebug : register(u22);
RWTexture2D<float4> g_rtDebug2 : register(u24);

ConstantBuffer<PathtracerConstantBuffer> CB : register(b0);          // ToDo standardize CB var naming
StructuredBuffer<PrimitiveMaterialBuffer> g_materials : register(t3);
StructuredBuffer<AlignedHemisphereSample3D> g_sampleSets : register(t4);

StructuredBuffer<float3x4> g_prevFrameBottomLevelASInstanceTransform : register(t15);


SamplerState LinearWrapSampler : register(s0);
SamplerComparisonState ShadowMapSamplerComp : register(s1);
SamplerState ShadowMapSampler : register(s2);

// ToDo: https://developer.nvidia.com/content/understanding-structured-buffer-performance
/*******************************************************************************************************/
// Per-object resources
ConstantBuffer<PrimitiveConstantBuffer> l_materialCB : register(b0, space1);

#if ONLY_SQUID_SCENE_BLAS
StructuredBuffer<Index> l_indices : register(t0, space1);
StructuredBuffer<VertexPositionNormalTextureTangent> l_vertices : register(t1, space1); // Current frame vertex buffer.
StructuredBuffer<VertexPositionNormalTextureTangent> l_verticesPrevFrame : register(t2, space1); 
#else
ByteAddressBuffer l_indices : register(t0, space1);
StructuredBuffer<VertexPositionNormalTexture> l_vertices : register(t1, space1); // Current frame vertex buffer.
StructuredBuffer<VertexPositionNormalTexture> l_verticesPrevFrame : register(t2, space1);
#endif
Texture2D<float3> l_texDiffuse : register(t3, space1);
Texture2D<float3> l_texNormalMap : register(t4, space1);
/*******************************************************************************************************/


// ToDo move

float GetPlaneConstant(in float3 planeNormal, in float3 pointOnThePlane)
{
    // Given a plane equation N * P + d = 0
    // d = - N * P
    return -dot(planeNormal, pointOnThePlane);
}

bool IsPointOnTheNormalSideOfPlane(in float3 P, in float3 planeNormal, in float3 pointOnThePlane)
{
    float d = GetPlaneConstant(planeNormal, pointOnThePlane);
    return dot(P, planeNormal) + d > 0;    // ToDo > ? >=
}

float3 ReflectPointThroughPlane(in float3 P, in float3 planeNormal, in float3 pointOnThePlane)
{
    //           |
    //           |
    //  P ------ C ------ R
    //           |
    //           |
    // Given a point P, plane with normal N and constant d, the projection point C of P onto plane is:
    // C = P + t*N
    //
    // Then the reflected point R of P through the plane can be computed using t as:
    // R = P + 2*t*N

    // Given C = P + t*N, and C lying on the plane,
    // C*N + d = 0
    // then
    // C = - d/N
    // -d/N = P + t*N
    // 0 = d + P*N + t*N*N
    // t = -(d + P*N) / N*N

    float d = GetPlaneConstant(planeNormal, pointOnThePlane);
    float3 N = planeNormal;
    float t = -(d + dot(P, N)) / dot(N, N);

    return P + 2 * t * N;
}


// Reflects a point across a planar mirror. 
// Returns FLT_MAX if the point is behind the mirror.
float3 ReflectFrontPointThroughPlane(
    in float3 p,
    in float3 mirrorSurfacePoint,
    in float3 mirrorNormal)
{
    if (!IsPointOnTheNormalSideOfPlane(p, mirrorNormal, mirrorSurfacePoint))
    {
        // ToDo attempt direct lookup using hit position in raygen instead of reflected point?
        return FLT_MAX; // ToDo is this safe?
    }

    return ReflectPointThroughPlane(p, mirrorNormal, mirrorSurfacePoint);
}

float3 GetWorldHitPositionInPreviousFrame(
    in float3 hitObjectPosition,
    in uint BLASInstanceIndex,
    in uint3 vertexIndices,
    in BuiltInTriangleIntersectionAttributes attr,
    out float3x4 _BLASTransform)
{
    // Variables prefixed with underscore _ denote values in the previous frame.

    // Calculate hit object position of the hit in the previous frame.
    float3 _hitObjectPosition;
    if (l_materialCB.isVertexAnimated)
    {
        float3 _vertices[3] = {
            l_verticesPrevFrame[vertexIndices[0]].position,
            l_verticesPrevFrame[vertexIndices[1]].position,
            l_verticesPrevFrame[vertexIndices[2]].position };
        _hitObjectPosition = HitAttribute(_vertices, attr);
    }
    else // non-vertex animated geometry        // ToDo apply this at declaration instead and avoid else?
    {
        _hitObjectPosition = hitObjectPosition;
    }

    // Transform the hit object position to world space.
    _BLASTransform = g_prevFrameBottomLevelASInstanceTransform[BLASInstanceIndex];
    return mul(_BLASTransform, float4(_hitObjectPosition, 1));
}

// Calculate a texture space motion vector from previous to current frame.
float2 CalculateMotionVector(
    in float3 _hitPosition,
    out float _depth,
    in uint2 DTid)
{
    // Variables prefixed with underscore _ denote values in the previous frame.
    float3 _hitViewPosition = _hitPosition - CB.prevCameraPosition;
    float3 _cameraDirection = GenerateForwardCameraRayDirection(CB.prevProjToWorldWithCameraEyeAtOrigin);
    _depth = dot(_hitViewPosition, _cameraDirection);

    // Calculate screen space position of the hit in the previous frame.
    float4 _clipSpacePosition = mul(float4(_hitPosition, 1), CB.prevViewProj);
    float2 _texturePosition = ClipSpaceToTexturePosition(_clipSpacePosition);

    // ToDO pass in inverted dimensions?
    // ToDo should this add 0.5f?
    float2 xy = DispatchRaysIndex().xy + 0.5f;   // Center in the middle of the pixel.
    float2 texturePosition = xy / DispatchRaysDimensions().xy;

    return texturePosition - _texturePosition;   
}

// ToDo cleanup matrix multiplication order



//***************************************************************************
//*****------ TraceRay wrappers for radiance and shadow rays. -------********
//***************************************************************************

// Trace a shadow ray and return true if it hits any geometry.
bool TraceShadowRayAndReportIfHit(out float tHit, in Ray ray, in UINT currentRayRecursionDepth, in bool retrieveTHit = true, in float TMax = 10000, in bool acceptFirstHit = false)
{
    if (currentRayRecursionDepth >= CB.maxShadowRayRecursionDepth)
    {
        return false;
    }

    // Set the ray's extents.
    RayDesc rayDesc;
    rayDesc.Origin = ray.origin;
    rayDesc.Direction = ray.direction;
    // Set TMin to a zero value to avoid aliasing artifacts along contact areas. // ToDo update comment re-floating error
    // Note: make sure to enable back-face culling so as to avoid surface face fighting.
    rayDesc.TMin = 0.0;
	rayDesc.TMax = TMax;

    // Initialize shadow ray payload.
    // Set the initial value to a hit at TMax. 
    // Miss shader will set it to HitDistanceOnMiss.
    // This way closest and any hit shaders can be skipped if true tHit is not needed. 
    ShadowRayPayload shadowPayload = { TMax };

    UINT rayFlags =
#if FACE_CULLING            // ToDo remove one path?
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES
#else
        0
#endif
        | RAY_FLAG_CULL_NON_OPAQUE;             // ~skip transparent objects
    
    if (acceptFirstHit || !retrieveTHit)
    {
        // Performance TIP: Accept first hit if true hit is not neeeded,
        // or has minimal to no impact. The peformance gain can
        // be substantial.
        // ToDo test perf impact
        rayFlags |= RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
    }

    // Skip closest hit shaders of tHit time is not needed.
    if (!retrieveTHit) 
    {
        rayFlags |= RAY_FLAG_SKIP_CLOSEST_HIT_SHADER; 
    }

    TraceRay(g_scene,
        rayFlags,
        TraceRayParameters::InstanceMask,
        TraceRayParameters::HitGroup::Offset[RayType::Shadow],
        TraceRayParameters::HitGroup::GeometryStride,
        TraceRayParameters::MissShader::Offset[RayType::Shadow],
        rayDesc, shadowPayload);
    
    // Report a hit if Miss Shader didn't set the value to HitDistanceOnMiss.
    tHit = shadowPayload.tHit;

    return shadowPayload.tHit > 0;
}

bool TraceShadowRayAndReportIfHit(out float tHit, in Ray ray, in float3 N, in UINT currentRayRecursionDepth, in bool retrieveTHit = true, in float TMax = 10000)
{
    // Only trace if the surface is facing the target.
    if (dot(ray.direction, N) > 0)
    {
        return TraceShadowRayAndReportIfHit(tHit, ray, currentRayRecursionDepth, retrieveTHit, TMax);
    }
    return false;
}

// Trace a camera ray into the scene.
// rx, ry - auxilary rays offset in screen space by one pixel in x, y directions.
#if USE_UV_DERIVATIVES
GBufferRayPayload TraceGBufferRay(in Ray ray, in Ray rx, in Ray ry, in UINT currentRayRecursionDepth, float tMin = NEAR_PLANE, float tMax = FAR_PLANE, float bounceContribution = 1, bool cullNonOpaque = false)
#else
GBufferRayPayload TraceGBufferRay(in Ray ray, in UINT currentRayRecursionDepth, float tMin = NEAR_PLANE, float tMax = FAR_PLANE, float bounceContribution = 1, bool cullNonOpaque = false)
#endif
{
    GBufferRayPayload rayPayload;
    rayPayload.rayRecursionDepth = currentRayRecursionDepth + 1;
    rayPayload.radiance = 0;
    rayPayload.AOGBuffer.tHit = HitDistanceOnMiss;
    rayPayload.AOGBuffer.hitPosition = 0;
    // rayPayload.materialInfo = 0;
    rayPayload.AOGBuffer.diffuseByte3 = 0;
    rayPayload.AOGBuffer.encodedNormal = 0;
    rayPayload.AOGBuffer._virtualHitPosition = 0;
    rayPayload.AOGBuffer._encodedNormal = 0; 
#if USE_UV_DERIVATIVES
    rayPayload.rx = rx;
    rayPayload.ry = ry;
#endif

    if (currentRayRecursionDepth >= CB.maxRadianceRayRecursionDepth)
    {
        return rayPayload;
    }

    // Set the ray's extents.
    RayDesc rayDesc;
    rayDesc.Origin = ray.origin;
    rayDesc.Direction = ray.direction;
    // ToDo update comments about Tmins
    // Set TMin to a zero value to avoid aliasing artifacts along contact areas.
    // Note: make sure to enable face culling so as to avoid surface face fighting.
    // ToDo Tmin - this should be offset along normal.
    rayDesc.TMin = tMin;
    rayDesc.TMax = tMax;

    UINT rayFlags =
#if FACE_CULLING
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES
#else
        0
#endif
        | (cullNonOpaque ? RAY_FLAG_CULL_NON_OPAQUE : 0);        


	TraceRay(g_scene,
        rayFlags,
		TraceRayParameters::InstanceMask,
		TraceRayParameters::HitGroup::Offset[RayType::GBuffer],
		TraceRayParameters::HitGroup::GeometryStride,
		TraceRayParameters::MissShader::Offset[RayType::GBuffer],
		rayDesc, rayPayload);

	return rayPayload;
}


Ray ReflectedRay(in float3 hitPosition, in float3 incidentDirection, in float3 normal)
{
    Ray reflectedRay;
    float smallValue = 1e-5f;
    reflectedRay.origin = hitPosition + normal * smallValue;
    reflectedRay.direction = reflect(incidentDirection, normal);

    return reflectedRay;
}

// Returns radiance of the traced ray.
// ToDo standardize variable names
float3 TraceReflectedGBufferRay(in float3 hitPosition, in float3 wi, in float3 N, in float3 objectNormal, inout GBufferRayPayload rayPayload, in float TMax = 10000)
{
    
    float tOffset = 0.001f;
    // ToDo offset in the ray direction so that the reflected ray projects to the same screen pixel. Otherwise it results in swimming in TAO. 
    float3 offsetAlongRay = tOffset * wi;

    float3 adjustedHitPosition = hitPosition + offsetAlongRay;


    // Intersection points of auxilary rays with the current surface
    // ToDo dedupe - this is already calculated in the closest hi
#if USE_UV_DERIVATIVES
    float3 px = RayPlaneIntersection(adjustedHitPosition, N, rayPayload.rx.origin, rayPayload.rx.direction);
    float3 py = RayPlaneIntersection(adjustedHitPosition, N, rayPayload.ry.origin, rayPayload.ry.direction);

    // Calculate reflected rx, ry
    // ToDo safeguard this agianst reflection going behind due to grazing angles
    Ray rx = { px, reflect(rayPayload.rx.direction, N) };
    Ray ry = { py, reflect(rayPayload.ry.direction, N) };
#endif

#if CALCULATE_PARTIAL_DEPTH_DERIVATIVES_IN_RAYGEN
    float3 rxRaySegment = px - rayPayload.rx.origin;
    float3 ryRaySegment = py - rayPayload.ry.origin;
    rayPayload.rxTHit += length(rxRaySegment);
    rayPayload.ryTHit += length(ryRaySegment);
#endif

    // ToDo offset along surface normal, and adjust tOffset subtraction below.
    Ray ray = { adjustedHitPosition,  wi };

    float tMin = 0; // NEAR_PLANE ToDo
    float tMax = TMax;  //  FAR_PLANE - RayTCurrent()

#if USE_UV_DERIVATIVES
    rayPayload = TraceGBufferRay(ray, rx, ry, rayPayload.rayRecursionDepth, tMin, tMax);
#else
    rayPayload = TraceGBufferRay(ray, rayPayload.rayRecursionDepth, tMin, tMax);
#endif
    if (rayPayload.AOGBuffer.tHit != HitDistanceOnMiss)
    {
        // Get the current planar mirror in the previous frame.
        float3x4 _mirrorBLASTransform = g_prevFrameBottomLevelASInstanceTransform[InstanceIndex()];
        float3 _mirrorHitPosition = mul(_mirrorBLASTransform, float4(HitObjectPosition(), 1));

        // Pass the virtual hit position reflected across the current mirror surface upstream 
        // as if the ray went through the mirror to be able to recursively reflect at correct ray depths and then projecting to the screen.
        // Skipping normalization as it's not required for the uses of the transformed normal here.
        float3 _mirrorNormal = mul((float3x3)_mirrorBLASTransform, objectNormal);

        //g_rtDebug[DispatchRaysIndex().xy] = float4(_mirrorHitPosition, 0);
        //g_rtDebug2[DispatchRaysIndex().xy] = float4(_mirrorNormal, 0);
        rayPayload.AOGBuffer._virtualHitPosition = ReflectFrontPointThroughPlane(rayPayload.AOGBuffer._virtualHitPosition, _mirrorHitPosition, _mirrorNormal);

        // Add current thit and the added offset to the thit of the traced ray.
        rayPayload.AOGBuffer.tHit += RayTCurrent() + tOffset;
    }

    return rayPayload.radiance;
}

// Returns radiance of the traced ray.
// ToDo standardize variable names
float3 TraceRefractedGBufferRay(in float3 hitPosition, in float3 wt, in float3 N, in float3 objectNormal, inout GBufferRayPayload rayPayload, in float TMax = 10000)
{
    float tOffset = 0.001f;
    float3 adjustedHitPosition = hitPosition + tOffset * wt;
    
#if USE_UV_DERIVATIVES
    float3 px = RayPlaneIntersection(adjustedHitPosition, N, rayPayload.rx.origin, rayPayload.rx.direction);
    float3 py = RayPlaneIntersection(adjustedHitPosition, N, rayPayload.ry.origin, rayPayload.ry.direction);

    // ToDo currently refracted rays are simply transparent rays
    Ray rx = { px, rayPayload.rx.direction };
    Ray ry = { py, rayPayload.ry.direction };
#endif

#if CALCULATE_PARTIAL_DEPTH_DERIVATIVES_IN_RAYGEN
    float3 rxRaySegment = px - rayPayload.rx.origin;
    float3 ryRaySegment = py - rayPayload.ry.origin;
    rayPayload.rxTHit += length(rxRaySegment);
    rayPayload.ryTHit += length(ryRaySegment);
#endif

    // ToDo offset along surface normal, and adjust tOffset subtraction below.
    Ray ray = { adjustedHitPosition,  wt };

    float tMin = 0; // NEAR_PLANE ToDo
    float tMax = TMax;  //  FAR_PLANE - RayTCurrent()

    // Performance vs visual quality trade-off:
    // Cull transparent surfaces when casting a transmission ray for a transparent surface.
    // Spaceship in particular has multiple layer glass causing a substantial perf hit (30ms on closeup) with multiple bounces along the way.
    // This can cause visual pop ins however, such as in a case of looking at the spaceship's glass cockpit through a window in the house. The cockpit will be skipped in this case.
    bool cullNonOpaque = true;

#if USE_UV_DERIVATIVES
    rayPayload = TraceGBufferRay(ray, rayPayload.rx, rayPayload.ry, rayPayload.rayRecursionDepth, tMin, tMax, 0, cullNonOpaque);
#else
    rayPayload = TraceGBufferRay(ray, rayPayload.rayRecursionDepth, tMin, tMax, 0, cullNonOpaque);
#endif
    if (rayPayload.AOGBuffer.tHit != HitDistanceOnMiss)
    {
        // Add current thit and the added offset to the thit of the traced ray.
        rayPayload.AOGBuffer.tHit += RayTCurrent() + tOffset;
    }

    return rayPayload.radiance;
}



bool TraceShadowRayAndReportIfHit(in float3 hitPosition, in float3 direction, in float3 N, in GBufferRayPayload rayPayload, in float TMax = 10000)
{
    float tOffset = 0.001f;
    Ray visibilityRay = { hitPosition + tOffset * N, direction };
    float dummyTHit;    // ToDo remove
    return TraceShadowRayAndReportIfHit(dummyTHit, visibilityRay, N, rayPayload.rayRecursionDepth, false, TMax);
}


// Update AO GBuffer with the hit that has the largest diffuse component.
// Prioritize larger diffuse component hits as it is a direct scale of the AO contribution to the final color value.
// This doesn't always result in the largest AO contribution as the final color contribution depends on the AO coefficient as well,
// but this is the best estimate at this stage.
void UpdateAOGBufferOnLargerDiffuseComponent(inout GBufferRayPayload rayPayload, in GBufferRayPayload _rayPayload, in float3 diffuseScale)
{
    float3 diffuse = Byte3ToNormalizedFloat3(rayPayload.AOGBuffer.diffuseByte3);

    // Adjust the diffuse by the diffuse scale, i.e. BRDF value of the returned ray.
    float3 _diffuse = Byte3ToNormalizedFloat3(_rayPayload.AOGBuffer.diffuseByte3) * diffuseScale;
    
    if (_rayPayload.AOGBuffer.tHit != HitDistanceOnMiss && RGBtoLuminance(diffuse) < RGBtoLuminance(_diffuse))
    {
        rayPayload.AOGBuffer = _rayPayload.AOGBuffer;
        rayPayload.AOGBuffer.diffuseByte3 = NormalizedFloat3ToByte3(_diffuse);
    }
}


float3 Shade(
    inout GBufferRayPayload rayPayload,
    in float3 N,
    in float3 objectNormal, // ToDo N vs normal
    in float3 hitPosition,
    in PrimitiveMaterialBuffer material)
{
    float3 V = -WorldRayDirection();
    float pdf;
    float3 indirectContribution = 0;
    float3 L = 0;

    const float3 Kd = material.Kd;
    const float3 Ks = material.Ks;
    const float3 Kr = material.Kr;
    const float3 Kt = material.Kt;
    const float roughness = material.roughness;    // ToDo Roughness of 0.001 loses specular - precision? 

     // Direct illumination
    rayPayload.AOGBuffer.diffuseByte3 = NormalizedFloat3ToByte3(Kd);    // ToDo use BRDF instead?
    if (!BxDF::IsBlack(material.Kd) || !BxDF::IsBlack(material.Ks))
    {
        // ToDo dedupe wi calculation
        float3 wi = normalize(CB.lightPosition.xyz - hitPosition);

        // Raytraced shadows.
        bool isInShadow = TraceShadowRayAndReportIfHit(hitPosition, wi, N, rayPayload);

        L += BxDF::DirectLighting::Shade(
            // ToDo have a substruct to pass around?
            material.type,
            Kd,
            Ks,
            CB.lightColor.xyz,
            isInShadow,
            roughness,
            N,
            V,
            wi);
    }

    // Ambient Indirect Illumination
    // Add a default ambient contribution to all hits. 
    // This will be subtracted for hitPositions with 
    // calculated Ambient coefficient in the composition pass.
    L += CB.defaultAmbientIntensity * Kd;

    // Specular Indirect Illumination
    bool isReflective = !BxDF::IsBlack(Kr);
    bool isTransmissive = !BxDF::IsBlack(Kt);

    // Handle cases where ray is coming from behind due to imprecision,
    // don't cast reflection rays in that case.
    float smallValue = 1e-6f;
    isReflective = dot(V, N) > smallValue ? isReflective : false;

    if (isReflective || isTransmissive)
    {
        if (isReflective 
            && (BxDF::Specular::Reflection::IsTotalInternalReflection(V, N) 
                || material.type == MaterialType::Mirror))
        {
            GBufferRayPayload reflectedRayPayLoad = rayPayload;
            float3 wi = reflect(-V, N);
                
            L += Kr * TraceReflectedGBufferRay(hitPosition, wi, N, objectNormal, reflectedRayPayLoad);
            UpdateAOGBufferOnLargerDiffuseComponent(rayPayload, reflectedRayPayLoad, Kr);
        }
        else // No total internal reflection
        {
            float3 Fo = Ks;
            if (isReflective)
            {
                // Radiance contribution from reflection.
                float3 wi;
                float3 Fr = Kr * BxDF::Specular::Reflection::Sample_Fr(V, wi, N, Fo);    // Calculates wi
                
                GBufferRayPayload reflectedRayPayLoad = rayPayload;
                // Ref: eq 24.4, RTG
                L += Fr * TraceReflectedGBufferRay(hitPosition, wi, N, objectNormal, reflectedRayPayLoad);
                UpdateAOGBufferOnLargerDiffuseComponent(rayPayload, reflectedRayPayLoad, Fr);
            }

            if (isTransmissive)
            {
                // Radiance contribution from refraction.
                float3 wt;
                float3 Ft = Kt * BxDF::Specular::Transmission::Sample_Ft(V, wt, N, Fo);    // Calculates wt

                GBufferRayPayload refractedRayPayLoad = rayPayload;

                L += Ft * TraceRefractedGBufferRay(hitPosition, wt, N, objectNormal, refractedRayPayLoad);
                UpdateAOGBufferOnLargerDiffuseComponent(rayPayload, refractedRayPayLoad, Ft);
            }
        }
    }

    return L;
}

//***************************************************************************
//********************------ Ray gen shader.. -------************************
//***************************************************************************

[shader("raygeneration")]
void MyRayGenShader_GBuffer()
{
    uint2 DTid = DispatchRaysIndex().xy;
    // ToDo make sure all pixels get written to or clear buffers beforehand. 

	// Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
	Ray ray = GenerateCameraRay(DTid, CB.cameraPosition, CB.projectionToWorldWithCameraEyeAtOrigin);

#if USE_UV_DERIVATIVES
    Ray rx, ry;
    GetAuxilaryCameraRays(CB.cameraPosition, CB.projectionToWorldWithCameraEyeAtOrigin, rx, ry);
#endif

	// Cast a ray into the scene and retrieve GBuffer information.
	UINT currentRayRecursionDepth = 0;
#if USE_UV_DERIVATIVES
	GBufferRayPayload rayPayload = TraceGBufferRay(ray, rx, ry, currentRayRecursionDepth);
#else
    GBufferRayPayload rayPayload = TraceGBufferRay(ray, currentRayRecursionDepth);
#endif


    // Invalidate perfect mirror reflections that missed. 
    // There is no We don't need to calculate AO for those.
    bool hasNonZeroDiffuse = rayPayload.AOGBuffer.diffuseByte3 != 0;
    rayPayload.AOGBuffer.tHit = hasNonZeroDiffuse ? rayPayload.AOGBuffer.tHit : HitDistanceOnMiss;
    bool hasCameraRayHitGeometry = rayPayload.AOGBuffer.tHit > 0;   // ToDo use helper fcn


	// Write out GBuffer information to rendertargets.
	// ToDo Test conditional write on all output 
	g_rtGBufferCameraRayHits[DTid] = hasCameraRayHitGeometry ? 1 : 0;   // ToDo should this be 1 if we hit a perfect mirror reflecting into skybox?

    // g_rtGBufferMaterialInfo[DTid] = rayPayload.materialInfo;

    // ToDo Use calculated hitposition based on distance from GBuffer instead?
    g_rtGBufferPosition[DTid] = float4(rayPayload.AOGBuffer.hitPosition, 0);

    float rayLength = DISTANCE_ON_MISS;
    float obliqueness = 0;

#if  REPRO_BLOCKY_ARTIFACTS_NONUNIFORM_CB_REFERENCE_SSAO // CB value is incorrect on rayPayload.AOGBuffer.hit boundaries causing blocky artifacts when within if (hit) block
    if (rayPayload.AOGBuffer.hit)
    {
        float3 raySegment = rayPayload.hitPosition - CB.cameraPosition;
#else
    
    // 
    // ToDo dedupe
    //float4 viewSpaceHitPosition = float4(rayPayload.hitPosition - CB.cameraPosition, 1);
    if (hasCameraRayHitGeometry)
    {
#endif
       // float forwardFacing = dot(rayPayload.surfaceNormal, raySegment) / rayLength;
       // obliqueness = -forwardFacing;// min(f16tof32(0x7BFF), rcp(max(forwardFacing, 1e-5)));
#if OBLIQUENESS_IS_SURFACE_PLANE_DISTANCE_FROM_ORIGIN_ALONG_SHADING_NORMAL
        //obliqueness = -dot(rayPayload.surfaceNormal, rayPayload.hitPosition);
#endif
        rayLength = rayPayload.AOGBuffer.tHit;
        obliqueness = 0;// ToDo rayPayload.AOGBuffer.obliqueness;
    
        // Calculate the motion vector.
        float _depth;
        //g_rtDebug2[DTid] = float4(rayPayload.AOGBuffer._virtualHitPosition, 0);
        float2 motionVector = CalculateMotionVector(rayPayload.AOGBuffer._virtualHitPosition, _depth, DTid);
        g_rtTextureSpaceMotionVector[DTid] = motionVector;

#if USE_NORMALIZED_Z
        _depth = NormalizeToRange(_depth, CB.Znear, CB.Zfar);
#endif
        // ToDo remove the decode step
        g_rtReprojectedNormalDepth[DTid] = EncodeNormalDepth(DecodeNormal(rayPayload.AOGBuffer._encodedNormal), _depth);

        // ToDo normalize depth to [0,1] as floating point has higher precision around 0.
        // ToDo need to normalize hit distance as well
#if USE_NORMALIZED_Z
        float linearDistance = NormalizeToRange(rayLength, CB.Znear, CB.Zfar);
#else
        float linearDistance = rayLength;
#endif
    // Calculate z-depth
        float3 cameraDirection = GenerateForwardCameraRayDirection(CB.projectionToWorldWithCameraEyeAtOrigin);
        float linearDepth = linearDistance * dot(ray.direction, cameraDirection);

#if CALCULATE_PARTIAL_DEPTH_DERIVATIVES_IN_RAYGEN
        // ToDo recalculate rx.direction to avoid live state?
        float rxLinearDepth = rayPayload.rxTHit * dot(rx.direction, cameraDirection);
        float ryLinearDepth = rayPayload.ryTHit * dot(ry.direction, cameraDirection);
        float2 ddxy = abs(float2(rxLinearDepth, ryLinearDepth) - linearDepth);
        g_rtPartialDepthDerivatives[DTid] = ddxy;
#endif
#if 1
        float nonLinearDepth = rayPayload.AOGBuffer.tHit > 0 ?
            (FAR_PLANE + NEAR_PLANE - 2.0 * NEAR_PLANE * FAR_PLANE / linearDepth) / (FAR_PLANE - NEAR_PLANE)
            : 1;
        nonLinearDepth = (nonLinearDepth + 1.0) / 2.0;
        //linearDepth = rayLength = nonLinearDepth;
#endif

    // ToDo do we need both? Or just normal for high-fidelity one w/o depth?
        // ToDo remove the decode step
        g_rtGBufferNormalDepth[DTid] = EncodeNormalDepth(DecodeNormal(rayPayload.AOGBuffer.encodedNormal), linearDepth);

        g_rtGBufferDistance[DTid] = linearDepth;

        g_rtGBufferDepth[DTid] = linearDepth;

        g_rtAOSurfaceAlbedo[DTid] = float4(Byte3ToNormalizedFloat3(rayPayload.AOGBuffer.diffuseByte3), 0);
    }
    else // No geometry hit.
    {
        // ToDo skip unnecessary writes
        // ToDo use commonly defined values
        // Depth of 0 demarks no hit
        // as low precision normal depth resource R11G11B10 
        // can store non-negative numbers only.
        g_rtGBufferNormalDepth[DTid] = 0;

        // Invalidate the motion vector - set it to move well out of texture bounds.
        g_rtTextureSpaceMotionVector[DTid] = 1e3f;
        g_rtReprojectedNormalDepth[DTid] = 0;   // ToDo can we skip this write

    }

    g_rtColor[DTid] = float4(rayPayload.radiance, 1);
}

//***************************************************************************
//******************------ Closest hit shaders -------***********************
//***************************************************************************
float3 NormalMap(
    in float3 normal,
    in float2 texCoord,
    in float2 ddx,
    in float2 ddy,
    in VertexPositionNormalTextureTangent vertices[3],
    in PrimitiveMaterialBuffer material,
    in BuiltInTriangleIntersectionAttributes attr)
{
    float3 tangent;
    if (material.hasPerVertexTangents)
    {
        float3 vertexTangents[3] = { vertices[0].tangent, vertices[1].tangent, vertices[2].tangent };
        tangent = HitAttribute(vertexTangents, attr);
    }
    else // ToDo precompute them for all geometry
    {
        float3 v0 = vertices[0].position;
        float3 v1 = vertices[1].position;
        float3 v2 = vertices[2].position;
        float2 uv0 = vertices[0].textureCoordinate;
        float2 uv1 = vertices[1].textureCoordinate;
        float2 uv2 = vertices[2].textureCoordinate;
        tangent = CalculateTangent(v0, v1, v2, uv0, uv1, uv2);
    }

    float3 bumpNormal = normalize(l_texNormalMap.SampleGrad(LinearWrapSampler, texCoord, ddx, ddy).xyz) * 2.f - 1.f;
    return BumpMapNormalToWorldSpaceNormal(bumpNormal, normal, tangent);
}

[shader("closesthit")]
void MyClosestHitShader_GBuffer(inout GBufferRayPayload rayPayload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint startIndex = PrimitiveIndex() * 3;
    const uint3 indices = { l_indices[startIndex], l_indices[startIndex + 1], l_indices[startIndex + 2] };

    // Retrieve vertices for the hit triangle.
    VertexPositionNormalTextureTangent vertices[3] = {
        l_vertices[indices[0]],
        l_vertices[indices[1]],
        l_vertices[indices[2]] };

    float2 vertexTexCoords[3] = { vertices[0].textureCoordinate, vertices[1].textureCoordinate, vertices[2].textureCoordinate };
    float2 texCoord = HitAttribute(vertexTexCoords, attr);

    UINT materialID = l_materialCB.materialID;
    PrimitiveMaterialBuffer material = g_materials[materialID];

    // Load triangle normal.
    float3 normal;
    float3 objectNormal;
    {
        // Retrieve corresponding vertex normals for the triangle vertices.
        float3 vertexNormals[3] = { vertices[0].normal, vertices[1].normal, vertices[2].normal };
        objectNormal = normalize(HitAttribute(vertexNormals, attr));    //ToDo normalization here is not needed

#if !FACE_CULLING
        float orientation = HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE ? 1 : -1;
        objectNormal *= orientation;
#endif

        // BLAS Transforms in this sample are uniformly scaled so it's OK to directly apply the BLAS transform.
        // ToDo add a note that the transform is expected to have uniform scaling
        normal = normalize(mul((float3x3)ObjectToWorld3x4(), objectNormal));
    }

    float3 hitPosition = HitWorldPosition();

    float2 ddx = 1;
    float2 ddy = 1;

#if USE_UV_DERIVATIVES
    // Calculate auxilary rays' intersection points with the triangle.
    float3 px, py;
    px = RayPlaneIntersection(hitPosition, normal, rayPayload.rx.origin, rayPayload.rx.direction);
    py = RayPlaneIntersection(hitPosition, normal, rayPayload.ry.origin, rayPayload.ry.direction);

    if (material.hasDiffuseTexture ||
        (CB.useNormalMaps && material.hasNormalTexture) ||
        (material.type == MaterialType::AnalyticalCheckerboardTexture))
    {
        float3 vertexTangents[3] = { vertices[0].tangent, vertices[1].tangent, vertices[2].tangent };
        float3 tangent = HitAttribute(vertexTangents, attr);
        float3 bitangent = normalize(cross(tangent, normal));

        CalculateUVDerivatives(normal, tangent, bitangent, hitPosition, px, py, ddx, ddy);
        // ToDo. Lower by 0.5 to sharpen texture filtering.
        //ddx *= 0.5;
        //ddy *= 0.5;
    }
#endif
    if (CB.useNormalMaps && material.hasNormalTexture)
    {
        normal = NormalMap(normal, texCoord, ddx, ddy, vertices, material, attr);
    }

    if (material.hasDiffuseTexture && !CB.useDiffuseFromMaterial)
    {
        material.Kd = RemoveSRGB(l_texDiffuse.SampleGrad(LinearWrapSampler, texCoord, ddx, ddy).xyz);
    }

    if (material.type == MaterialType::AnalyticalCheckerboardTexture)
    {
#if 0 
        float2 ddx_uv;
        float2 ddy_uv;
        float2 uv = TexCoords(hitPosition);

        CalculateRayDifferentials(ddx_uv, ddy_uv, uv, hitPosition, normal, CB.cameraPosition, CB.projectionToWorldWithCameraEyeAtOrigin);
        material.Kd = CheckersTextureBoxFilter(uv, ddx_uv, ddy_uv, 25);
#else // ToDo fix derivatives
        float2 uv = hitPosition.xz / 2;
        float checkers = CheckersTextureBoxFilter(uv, ddx, ddy);
        if (length(uv) < 45 && (checkers > 0.5))
        {
#if 1
            material.Kd = float3(21, 33, 45) / 255;
#else
            material.Kr = 1;
            material.Kd = 0;
#endif
        }
#endif
    }

    rayPayload.AOGBuffer.tHit = RayTCurrent();
    rayPayload.AOGBuffer.hitPosition = hitPosition;
    //rayPayload.materialInfo = EncodeMaterial16b(materialID, diffuse);
    rayPayload.AOGBuffer.encodedNormal = EncodeNormal(normal);

    // Calculate hit position and normal for the current hit in the previous frame.
    // Note: This is redundant if the AOGBuffer gets overwritten in the Shade function. 
    // However, delaying this computation to post-Shade which casts additional rays results 
    // in bigger live state carried across trace calls and thus higher overhead.
    {
        float3x4 _BLASTransform;
        rayPayload.AOGBuffer._virtualHitPosition = GetWorldHitPositionInPreviousFrame(HitObjectPosition(), InstanceIndex(), indices, attr, _BLASTransform);

        // Calculate normal at the hit in the previous frame.
        // BLAS Transforms in this sample are uniformly scaled so it's OK to directly apply the BLAS transform.
        // ToDo add a note that the transform is expected to have uniform scaling
        rayPayload.AOGBuffer._encodedNormal = EncodeNormal(normalize(mul((float3x3)_BLASTransform, objectNormal)));
    }

    // Shade the current hit point, including casting any further rays into the scene 
    // based on current's surface material properties.
    rayPayload.radiance = Shade(rayPayload, normal, objectNormal, hitPosition, material);
}

[shader("closesthit")]
void MyClosestHitShader_ShadowRay(inout ShadowRayPayload rayPayload, in BuiltInTriangleIntersectionAttributes attr)
{
    rayPayload.tHit = RayTCurrent();
}

//***************************************************************************
//**********************------ Miss shaders -------**************************
//***************************************************************************

// ToDo - remove miss shader for GBuffer
[shader("miss")]
void MyMissShader_GBuffer(inout GBufferRayPayload rayPayload)
{
    rayPayload.AOGBuffer.tHit = HitDistanceOnMiss;      // ToDo redundant

#if USE_ENVIRONMENT_MAP
    rayPayload.radiance = g_texEnvironmentMap.SampleLevel(LinearWrapSampler, WorldRayDirection(), 0).xyz;
#endif
}


[shader("miss")]
void MyMissShader_ShadowRay(inout ShadowRayPayload rayPayload)
{
    rayPayload.tHit = HitDistanceOnMiss;
}


#endif // RAYTRACING_HLSL