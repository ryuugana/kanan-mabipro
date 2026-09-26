// Sharp bilinear filtering for the scaled interface (compiled into UIScaleFilter.h):
//   fxc /nologo /T ps_2_0 /E main /Vn g_uiScaleFilter /Fh UIScaleFilter.h UIScaleFilter.hlsl
//
// Each texel is drawn as a solid block the size of the scale and only the seam between two
// texels is blended, so pixel art and bitmap fonts stay crisp with even strokes at any scale.
// Stands in for texture stage 0 of the fixed-function pipeline: the texture and the diffuse
// color are each either used or ignored (MODULATE, SELECTARG1 or SELECTARG2).

sampler tex : register(s0);

float4 texSize : register(c0);  // width, height, 1 / width, 1 / height
float4 sharpness : register(c1); // screen pixels per texel
float4 useTex : register(c2);    // 1 where the texture is used (rgb, rgb, rgb, a)
float4 useDiffuse : register(c3); // 1 where the diffuse color is used (rgb, rgb, rgb, a)

float4 main(float4 diffuse : COLOR0, float2 uv : TEXCOORD0) : COLOR {
    float2 texel = uv * texSize.xy - 0.5;
    float2 base = floor(texel);
    float2 blend = saturate((texel - base - 0.5) * sharpness.x + 0.5);
    float4 color = tex2D(tex, (base + blend + 0.5) * texSize.zw);

    return lerp(1.0, color, useTex) * lerp(1.0, diffuse, useDiffuse);
}
