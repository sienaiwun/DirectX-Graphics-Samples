#ifndef HEADER_HLSL
#define HEADER_HLSL

#define STR_INDIR(x) #x
#define STR(x) STR_INDIR(x)

#define CONCAT_B(x) b##x
#define CONCAT_T(x) t##x
#define CONCAT_S(x) s##x

#define CBUFFER(name, slot) \
cbuffer name : register(CONCAT_B(slot))

#define CBUFFER_VIEW(slot) \
"CBV(" STR(CONCAT_B(slot)) ")"

#define SLOT_CBUFFER_GAME           1
#define SLOT_CBUFFER_CAMERA         0
#define SLOT_CBUFFER_WORLD          2
#define SLOT_CBUFFER_SHADOW_LIGHT   3

#define SAMPLER_TEXTURE             0
#define SAMPLER_SHADOWMAP           1
#define SAMPLER_NUM                 2
#endif