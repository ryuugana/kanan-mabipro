// Pixel art upscaling for the scaled interface (compiled into UIScaleXbr.h):
//   fxc /nologo /T ps_2_a /E main /Vn g_uiScaleXbr /Fh UIScaleXbr.h UIScaleXbr.hlsl
//
// xBR level 2 (Hyllian), evaluated per screen pixel so it works at any scale: diagonal and
// curved edges in the interface's pixel art (bitmap text, icons) are redrawn as smooth lines
// at the output resolution while flat areas stay crisp. Edges are found on premultiplied color
// plus alpha, so shapes drawn in transparency (text) are detected too.
// Stands in for texture stage 0 of the fixed-function pipeline like UIScaleFilter.hlsl.
//
// xBR algorithm: Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com, MIT license:
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions: The above copyright notice
// and this permission notice shall be included in all copies or substantial portions of the
// Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.

sampler tex : register(s0);

float4 texSize : register(c0);    // width, height, 1 / width, 1 / height
float4 sharpness : register(c1);  // x: screen pixels per texel, y: 1 rounds corners, z: 1 smooths shallow slopes
float4 useTex : register(c2);     // 1 where the texture is used (rgb, rgb, rgb, a)
float4 useDiffuse : register(c3); // 1 where the diffuse color is used (rgb, rgb, rgb, a)

static const float4 Ao = float4(1.0, -1.0, -1.0, 1.0);
static const float4 Bo = float4(1.0, 1.0, -1.0, -1.0);
static const float4 Co = float4(1.5, 0.5, -0.5, 0.5);
static const float4 Ax = float4(1.0, -1.0, -1.0, 1.0);
static const float4 Bx = float4(0.5, 2.0, -0.5, -2.0);
static const float4 Cx = float4(1.0, 1.0, -0.5, 0.0);
static const float4 Ay = float4(1.0, -1.0, -1.0, 1.0);
static const float4 By = float4(2.0, 0.5, -2.0, -0.5);
static const float4 Cy = float4(2.0, 0.0, -1.0, 0.5);
static const float4 Ci = float4(0.25, 0.25, 0.25, 0.25);

static const float coef = 2.0;
static const float threshold = 0.06;

// Edge metric of a texel: premultiplied luma plus alpha.
float metric(float4 c) {
    return dot(c.rgb * c.a, float3(0.299, 0.587, 0.114)) + c.a * 0.5;
}

float4 metric4(float4 a, float4 b, float4 c, float4 d) {
    return float4(metric(a), metric(b), metric(c), metric(d));
}

float4 df(float4 a, float4 b) {
    return abs(a - b);
}

float4 neq(float4 a, float4 b) {
    return step(threshold, abs(a - b));
}

float4 eq(float4 a, float4 b) {
    return 1.0 - neq(a, b);
}

float4 wd(float4 a, float4 b, float4 c, float4 d, float4 e, float4 f, float4 g, float4 h) {
    return df(a, b) + df(a, c) + df(d, e) + df(d, f) + 4.0 * df(g, h);
}

float4 main(float4 diffuse : COLOR0, float2 uv : TEXCOORD0) : COLOR {
    float2 texel = uv * texSize.xy;
    float2 fp = frac(texel);
    float2 center = (floor(texel) + 0.5) * texSize.zw;
    float dx = texSize.z;
    float dy = texSize.w;

    float4 A1 = tex2D(tex, center + float2(-dx, -2.0 * dy));
    float4 B1 = tex2D(tex, center + float2(0.0, -2.0 * dy));
    float4 C1 = tex2D(tex, center + float2(dx, -2.0 * dy));
    float4 A = tex2D(tex, center + float2(-dx, -dy));
    float4 B = tex2D(tex, center + float2(0.0, -dy));
    float4 C = tex2D(tex, center + float2(dx, -dy));
    float4 D = tex2D(tex, center + float2(-dx, 0.0));
    float4 E = tex2D(tex, center);
    float4 F = tex2D(tex, center + float2(dx, 0.0));
    float4 G = tex2D(tex, center + float2(-dx, dy));
    float4 H = tex2D(tex, center + float2(0.0, dy));
    float4 I = tex2D(tex, center + float2(dx, dy));
    float4 G5 = tex2D(tex, center + float2(-dx, 2.0 * dy));
    float4 H5 = tex2D(tex, center + float2(0.0, 2.0 * dy));
    float4 I5 = tex2D(tex, center + float2(dx, 2.0 * dy));
    float4 A0 = tex2D(tex, center + float2(-2.0 * dx, -dy));
    float4 D0 = tex2D(tex, center + float2(-2.0 * dx, 0.0));
    float4 G0 = tex2D(tex, center + float2(-2.0 * dx, dy));
    float4 C4 = tex2D(tex, center + float2(2.0 * dx, -dy));
    float4 F4 = tex2D(tex, center + float2(2.0 * dx, 0.0));
    float4 I4 = tex2D(tex, center + float2(2.0 * dx, dy));

    float4 b = metric4(B, D, H, F);
    float4 c = metric4(C, A, G, I);
    float4 e = metric(E).xxxx;
    float4 d = b.yzwx;
    float4 f = b.wxyz;
    float4 g = c.zwxy;
    float4 h = b.zwxy;
    float4 i = c.wxyz;

    float4 i4 = metric4(I4, C1, A0, G5);
    float4 i5 = metric4(I5, C4, A1, G0);
    float4 h5 = metric4(H5, F4, B1, D0);
    float4 f4 = h5.yzwx;

    // Lines below which each corner is interpolated.
    float4 fx = Ao * fp.y + Bo * fp.x;
    float4 fxLeft = Ax * fp.y + Bx * fp.x;
    float4 fxUp = Ay * fp.y + By * fp.x;

    // Edges are antialiased over about one screen pixel, whatever the scale.
    float delta = 0.5 / max(sharpness.x, 1.0);

    float4 fx45i = saturate((fx + delta - Co - Ci) / (2.0 * delta));
    float4 fx45 = saturate((fx + delta - Co) / (2.0 * delta));
    float4 fx30 = saturate((fxLeft + delta - Cx) / (2.0 * delta));
    float4 fx60 = saturate((fxUp + delta - Cy) / (2.0 * delta));

    // Corners are kept square unless rounding is asked for: a corner is only smoothed when the
    // pixels around it continue a diagonal (xBR corner type 2).
    float4 continuesDiagonal = max(max(neq(f, b) * neq(h, d), eq(e, i) * neq(f, i4) * neq(h, i5)), max(eq(e, g), eq(e, c)));
    float4 restriction = neq(e, f) * neq(e, h) * lerp(continuesDiagonal, 1.0, sharpness.y);
    float4 restrictionLeft = neq(e, g) * neq(d, g);
    float4 restrictionUp = neq(e, c) * neq(b, c);

    float4 distance1 = wd(e, c, g, i, h5, f4, h, f);
    float4 distance2 = wd(h, d, i5, f, i4, b, e, i);

    float4 edri = step(distance1, distance2) * restriction;
    float4 edr = step(distance1 + 0.0001, distance2) * restriction;
    float4 edrLeft = step(coef * df(f, g), df(h, c)) * restrictionLeft * edr * sharpness.z;
    float4 edrUp = step(coef * df(h, c), df(f, g)) * restrictionUp * edr * sharpness.z;

    fx45 = edr * fx45;
    fx30 = edrLeft * fx30;
    fx60 = edrUp * fx60;
    fx45i = edri * fx45i;

    float4 px = step(df(e, f), df(e, h));
    float4 maximos = max(max(fx30, fx60), max(fx45, fx45i));

    float4 res1 = E;
    res1 = lerp(res1, lerp(H, F, px.x), maximos.x);
    res1 = lerp(res1, lerp(B, D, px.z), maximos.z);

    float4 res2 = E;
    res2 = lerp(res2, lerp(F, B, px.y), maximos.y);
    res2 = lerp(res2, lerp(D, H, px.w), maximos.w);

    float d1 = abs(metric(E) - metric(res1));
    float d2 = abs(metric(E) - metric(res2));
    float4 color = lerp(res1, res2, step(d1, d2));

    return lerp(1.0, color, useTex) * lerp(1.0, diffuse, useDiffuse);
}
