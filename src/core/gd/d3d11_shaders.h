// HLSL for the D3D11 backend's fixed-function emulation (compiled at run time
// with D3DCompile). Matrices arrive in GL layout, which HLSL's default
// column-major packing reads as written: mul(M, v) is GL's M * v.
#pragma once

namespace opensc4::gd {

// Must match DrawConstants in d3d11_backend.cpp.
constexpr char kFixedFunctionShaders[] = R"(
#define MAX_STAGES 8

struct Stage {
    uint4 setup;   // enabled, env mode, TexStageCoord value, has matrix
    uint4 rgb;     // combiner op, arg 0, arg 1, arg 2 (low nibble source, high nibble operand)
    uint4 alpha;
    float4 scale;  // rgb, alpha, sharp bilinear (interface images shown enlarged)
    float4x4 texMatrix;
};

cbuffer Draw : register(b0) {
    float4x4 modelView;
    float4x4 projection;   // already mapped to Direct3D's 0..1 depth range
    float4 colorMul;
    float4 texFactor;
    float4 fogColor;
    float4 fogParams;      // mode (0 exp, 1 exp2, 2 linear), density, start, end
    uint4 flags;           // x: lighting | 2 diffuse from vertex | 4 vertex has colour; y: vertex colours; z: fog; w: stage count
    uint4 alphaTest;       // enabled, function, reference (0-255)
    Stage stages[MAX_STAGES];
};

Texture2D textures[MAX_STAGES] : register(t0);
SamplerState samplers[MAX_STAGES] : register(s0);

struct VSIn {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float2 tc[MAX_STAGES] : TEXCOORD0;
};

struct VSOut {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float fog : FOG;
    float4 tc[MAX_STAGES] : TEXCOORD0;
};

VSOut VSMain(VSIn v) {
    VSOut o;
    float4 eye = mul(modelView, float4(v.pos, 1));
    o.pos = mul(projection, eye);

    // The DirectX driver applies ColorMultiplier/AlphaMultiplier as Direct3D
    // lighting with only an ambient light: colour = ambient material (vertex
    // colour or white) * ambient light, alpha = the diffuse material's (vertex
    // or material alpha). A vertex without colour uses the material.
    float4 c = v.color;
    if (flags.x & 1) {
        bool hasColor = (flags.x & 4) != 0;
        float3 ambient = flags.y && hasColor ? v.color.rgb : float3(1, 1, 1);
        c.rgb = saturate(ambient * colorMul.rgb);
        c.a = (flags.x & 2) && hasColor ? v.color.a : colorMul.a;
    }
    o.color = c;

    float3 n = normalize(mul((float3x3)modelView, v.normal));
    [unroll] for (uint k = 0; k < MAX_STAGES; ++k) {
        uint source = stages[k].setup.z;
        uint gen = source & ~7u;
        float4 t = float4(v.tc[source & 7], 0, 1);
        if (gen == 0x10) t = float4(eye.xyz, 1);
        else if (gen == 0x20) t = float4(n, 1);
        else if (gen == 0x30) t = float4(reflect(normalize(eye.xyz), n), 1);
        if (stages[k].setup.w) t = mul(stages[k].texMatrix, t);
        o.tc[k] = t;
    }

    float d = abs(eye.z);
    float f = 1;
    if (fogParams.x == 2) f = (fogParams.w - d) / (fogParams.w - fogParams.z);
    else if (fogParams.x == 0) f = exp(-fogParams.y * d);
    else if (fogParams.x == 1) { float e = fogParams.y * d; f = exp(-e * e); }
    o.fog = saturate(f);
    return o;
}

struct PSIn {
    float4 pos : SV_Position;
    COLOR_INTERPOLATION float4 color : COLOR0;
    float fog : FOG;
    float4 tc[MAX_STAGES] : TEXCOORD0;
};

float4 Source(uint arg, float4 t, float4 prev, float4 primary) {
    uint s = arg & 15;
    return s == 0 ? t : s == 1 ? prev : s == 2 ? texFactor : primary;
}

float3 OperandRGB(uint arg, float4 v) {
    uint o = arg >> 4;
    return o == 0 ? v.rgb : o == 1 ? 1 - v.rgb : o == 2 ? v.aaa : 1 - v.aaa;
}

float OperandA(uint arg, float4 v) {
    return ((arg >> 4) & 1) ? 1 - v.a : v.a;
}

float3 CombineRGB(uint op, float3 a0, float3 a1, float3 a2) {
    if (op == 0) return a0;
    if (op == 1) return a0 * a1;
    if (op == 2) return a0 + a1;
    if (op == 3) return a0 + a1 - 0.5;
    if (op == 4) return a0 * a2 + a1 * (1 - a2);
    return 4 * dot(a0 - 0.5, a1 - 0.5).xxx;
}

float CombineA(uint op, float a0, float a1, float a2) {
    if (op == 0) return a0;
    if (op == 1) return a0 * a1;
    if (op == 2) return a0 + a1;
    if (op == 3) return a0 + a1 - 0.5;
    if (op == 4) return a0 * a2 + a1 * (1 - a2);
    return a0;
}

bool Compare(uint func, uint a, uint ref) {
    if (func == 0) return false;
    if (func == 1) return a < ref;
    if (func == 2) return a == ref;
    if (func == 3) return a <= ref;
    if (func == 4) return a > ref;
    if (func == 5) return a != ref;
    if (func == 6) return a >= ref;
    return true;
}

float4 PSMain(PSIn i) : SV_Target {
    float4 primary = i.color;
    float4 prev = primary;
    [unroll] for (uint k = 0; k < MAX_STAGES; ++k) {
        float2 uv = i.tc[k].xy;
        float2 dx = ddx(uv), dy = ddy(uv);
        if (k < flags.w && stages[k].setup.x) {
            float4 t;
            if (stages[k].scale.z != 0) {
                // Sharp bilinear: each texel stays a flat square, only a band
                // of about a pixel at its edges is blended with the neighbour.
                float w, h;
                textures[k].GetDimensions(w, h);
                float2 size = float2(w, h);
                float2 texel = uv * size;
                float2 perPixel = max(abs(dx * size) + abs(dy * size), 1e-4); // texels per screen pixel
                float2 pixelsPerTexel = max(1.0 / perPixel, 1.0);
                float2 base = floor(texel);
                float2 centre = texel - base - 0.5;
                float2 range = 0.5 - 0.5 / pixelsPerTexel;
                float2 f = (centre - clamp(centre, -range, range)) * pixelsPerTexel + 0.5;
                t = textures[k].SampleLevel(samplers[k], (base + f) / size, 0);
            } else {
                t = textures[k].SampleGrad(samplers[k], uv, dx, dy);
            }
            uint env = stages[k].setup.y;
            float4 r;
            if (env == 0) r = t;                                               // replace
            else if (env == 1) r = prev * t;                                   // modulate
            else if (env == 2) r = float4(lerp(prev.rgb, t.rgb, t.a), prev.a); // decal
            else if (env == 3) r = float4(lerp(prev.rgb, texFactor.rgb, t.rgb), prev.a * t.a); // blend
            else {                                                             // combine
                uint4 cr = stages[k].rgb, ca = stages[k].alpha;
                float3 rgb = CombineRGB(cr.x, OperandRGB(cr.y, Source(cr.y, t, prev, primary)),
                                        OperandRGB(cr.z, Source(cr.z, t, prev, primary)),
                                        OperandRGB(cr.w, Source(cr.w, t, prev, primary)));
                float a = CombineA(ca.x, OperandA(ca.y, Source(ca.y, t, prev, primary)),
                                   OperandA(ca.z, Source(ca.z, t, prev, primary)),
                                   OperandA(ca.w, Source(ca.w, t, prev, primary)));
                r = float4(rgb * stages[k].scale.x, a * stages[k].scale.y);
            }
            prev = saturate(r);
        }
    }
    if (alphaTest.x && !Compare(alphaTest.y, (uint)round(saturate(prev.a) * 255), alphaTest.z))
        discard;
    if (flags.z)
        prev.rgb = lerp(fogColor.rgb, prev.rgb, i.fog);
    return prev;
}
)";

} // namespace opensc4::gd
