
void main(
    in uint VertID : SV_VertexID,
    out float4 Pos : SV_Position,
    out float2 Tex : TexCoord0
)
{
    // Texture coordinates range [0, 2], but only [0, 1] appears on screen.
    Tex = float2(VertID % 2, (VertID % 4) >> 1);
    Pos = float4((Tex.x - 0.5f) * 2, -(Tex.y - 0.5f) * 2, 0, 1);
}

