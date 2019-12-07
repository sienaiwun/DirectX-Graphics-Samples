
#define Sky_RootSig \
     "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |"\
    "DENY_HULL_SHADER_ROOT_ACCESS|"\
    "DENY_DOMAIN_SHADER_ROOT_ACCESS| "\
    "DENY_GEOMETRY_SHADER_ROOT_ACCESS), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)," \
    "StaticSampler(s1," \
        "addressU = TEXTURE_ADDRESS_BORDER," \
        "addressV = TEXTURE_ADDRESS_BORDER," \
        "addressW = TEXTURE_ADDRESS_BORDER," \
        "borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"