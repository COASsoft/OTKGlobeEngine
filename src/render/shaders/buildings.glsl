@vs building_vs
layout(binding=0) uniform building_vs_params {
    mat4 mvp;
    vec4 params;
};

in vec3 position;
in vec3 normal;
in vec4 texcoord0;
in vec4 color0;
in vec2 texcoord1;

out vec3 nrm;
out vec4 uvw;
out vec4 tint;
out float kind;

void main() {
    vec3 p = position;
    float base = texcoord1.x;
    p.z = base + (p.z - base) * params.x;
    gl_Position = mvp * vec4(p, 1.0);
    nrm = normal;
    uvw = texcoord0;
    tint = color0;
    kind = texcoord1.y;
}
@end

@fs building_fs
layout(binding=0) uniform texture2DArray facades;
layout(binding=1) uniform texture2D roofs;
layout(binding=0) uniform sampler facade_smp;
layout(binding=1) uniform sampler roof_smp;

layout(binding=1) uniform building_fs_params {
    vec4 light;
    vec4 shading;
    vec4 sizes[12];
};

in vec3 nrm;
in vec4 uvw;
in vec4 tint;
in float kind;
out vec4 frag_color;

float band(float x, float lo, float hi, float aa) {
    return smoothstep(lo - aa, lo + aa, x) * (1.0 - smoothstep(hi - aa, hi + aa, x));
}

vec3 windows(vec3 wall, vec2 m, float len, float style) {
    float storey = style < 1.5 ? 3.0 : (style < 2.5 ? 3.4 : 5.0);
    float spacing = style < 1.5 ? 3.4 : (style < 2.5 ? 2.8 : 7.5);
    float width = style < 1.5 ? 1.15 : (style < 2.5 ? 1.7 : 4.8);
    float bottom = style < 2.5 ? 0.95 : 3.3;
    float top = style < 2.5 ? 2.3 : 4.3;
    float count = floor(len / spacing);
    if (count < 1.0) return wall;
    float x = m.x - (len - count * spacing) * 0.5;
    if (x < 0.0 || x > count * spacing) return wall;
    float aa = max(fwidth(m.x), fwidth(m.y)) * 0.75 + 0.01;
    float cx = abs(mod(x, spacing) - spacing * 0.5);
    float fy = mod(m.y, storey);
    float frameW = 0.09;
    float inner = band(cx, -1.0, width * 0.5 - frameW, aa) * band(fy, bottom + frameW, top - frameW, aa);
    float outer = band(cx, -1.0, width * 0.5, aa) * band(fy, bottom, top, aa);
    float fade = 1.0 - smoothstep(0.35, 0.9, aa);
    vec3 glass = mix(vec3(0.13, 0.16, 0.2), vec3(0.42, 0.5, 0.58), clamp((fy - bottom) / (top - bottom), 0.0, 1.0) * 0.55);
    vec3 frame = mix(wall, vec3(0.92, 0.92, 0.9), 0.7);
    vec3 c = mix(wall, frame, outer * fade);
    return mix(c, glass, inner * fade);
}

void main() {
    vec3 n = normalize(nrm);
    float diffuse = max(dot(n, light.xyz), 0.0);
    if (kind > 0.5) {
        vec3 roof = shading.y > 0.5 ? texture(sampler2D(roofs, roof_smp), uvw.xy).rgb
                                    : texture(sampler2DArray(facades, facade_smp), vec3(uvw.xy * 40.0, 11.0)).rgb;
        float lit = mix(1.0, 0.82 + 0.3 * diffuse, clamp(1.0 - n.z, 0.0, 1.0) * 2.5);
        frag_color = vec4(roof * lit, 1.0);
        return;
    }
    int layer = int(uvw.w + 0.5);
    vec4 size = sizes[layer];
    vec2 m = vec2(uvw.x, max(uvw.y, 0.0));
    vec3 wall = texture(sampler2DArray(facades, facade_smp), vec3(m.x / size.x, 1.0 - m.y / size.y, uvw.w)).rgb * tint.rgb;
    if (size.z > 0.5) wall = windows(wall, m, uvw.z, size.z);
    float ao = mix(0.68, 1.0, clamp(m.y / 5.0, 0.0, 1.0));
    float lit = shading.x + light.w * diffuse;
    frag_color = vec4(wall * lit * ao, 1.0);
}
@end

@program building building_vs building_fs

@vs roofbake_vs
in vec2 position;
in vec2 texcoord0;
out vec2 uv;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = texcoord0;
}
@end

@fs roofbake_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = vec4(texture(sampler2D(tex, smp), uv).rgb, 1.0);
}
@end

@program roofbake roofbake_vs roofbake_fs
