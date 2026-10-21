@vs globe_vs
layout(binding=0) uniform globe_vs_params {
    mat4 mvp;
    vec4 uv_xform;
    vec4 uv_xform2;
    vec4 uv_xform3;
    vec4 skirt;
    vec4 center;
    vec4 edges;
    vec4 eyerel;
    vec4 morph;
};

in vec3 position;
in vec2 texcoord0;
in float shade;
in float height;

out vec2 uv;
out vec2 uv2;
out vec2 uv3;
out float light;
out float elev;
out float lodw;
out float morphw;

void main() {
    vec3 p = position;
    if (float(gl_VertexIndex) >= skirt.y) p -= normalize(center.xyz + position) * skirt.x;
    gl_Position = mvp * vec4(p, 1.0);
    uv = texcoord0 * uv_xform.xy + uv_xform.zw;
    uv2 = texcoord0 * uv_xform2.xy + uv_xform2.zw;
    uv3 = texcoord0 * uv_xform3.xy + uv_xform3.zw;
    float mw = 0.0;
    if (morph.w > 0.0) {
        vec3 rel = position + eyerel.xyz;
        float len = max(length(rel), 1.0);
        float facing = max(abs(dot(rel / len, normalize(center.xyz + position))), 0.35);
        float d = max(len - eyerel.w, 1.0);
        mw = clamp((morph.y - morph.x * facing / d) / morph.z, 0.0, 1.0) * morph.w;
    }
    morphw = mw;
    light = shade;
    elev = height;
    float w = 1.0;
    if (edges.x > 0.5) w = min(w, texcoord0.y * 2.0);
    if (edges.y > 0.5) w = min(w, (1.0 - texcoord0.y) * 2.0);
    if (edges.z > 0.5) w = min(w, texcoord0.x * 2.0);
    if (edges.w > 0.5) w = min(w, (1.0 - texcoord0.x) * 2.0);
    lodw = clamp(w, 0.0, 1.0);
}
@end

@fs globe_fs
layout(binding=1) uniform globe_fs_params {
    vec4 flags;
    vec4 heat;
    vec4 slope;
};

layout(binding=0) uniform texture2D tex;
layout(binding=1) uniform texture2D tex2;
layout(binding=2) uniform texture2D tex3;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec2 uv2;
in vec2 uv3;
in float light;
in float elev;
in float lodw;
in float morphw;
out vec4 frag_color;

vec3 ramp(float x) {
    x = clamp(x, 0.0, 1.0);
    vec3 a = mix(vec3(0.0, 0.2, 0.8), vec3(0.0, 0.75, 0.3), smoothstep(0.0, 0.25, x));
    vec3 b = mix(a, vec3(0.95, 0.9, 0.1), smoothstep(0.25, 0.5, x));
    vec3 c = mix(b, vec3(0.9, 0.4, 0.05), smoothstep(0.5, 0.75, x));
    return mix(c, vec3(0.95, 0.95, 0.95), smoothstep(0.75, 1.0, x));
}

vec3 slopeRamp(float x) {
    x = clamp(x, 0.0, 1.0);
    vec3 a = mix(vec3(1.0, 0.95, 0.25), vec3(1.0, 0.55, 0.1), smoothstep(0.0, 0.35, x));
    vec3 b = mix(a, vec3(0.8, 0.1, 0.05), smoothstep(0.35, 0.7, x));
    return mix(b, vec3(0.05, 0.05, 0.05), smoothstep(0.7, 1.0, x));
}

void main() {
    vec2 t = vec2(uv.x, flags.x > 0.5 ? 1.0 - uv.y : uv.y);
    float s = mix(1.0, min(0.25 + 0.75 * light, 1.0), flags.y);
    vec2 t2 = vec2(uv2.x, flags.x > 0.5 ? 1.0 - uv2.y : uv2.y);
    vec2 t3 = vec2(uv3.x, flags.x > 0.5 ? 1.0 - uv3.y : uv3.y);
    vec3 own = mix(texture(sampler2D(tex2, smp), t2).rgb, texture(sampler2D(tex, smp), t).rgb, flags.z);
    if (morphw > 0.0) {
        vec3 lowOwn = textureLod(sampler2D(tex, smp), t, 4.0).rgb;
        vec3 lowParent = textureLod(sampler2D(tex3, smp), t3, 3.0).rgb;
        own = clamp(own + morphw * (lowParent - lowOwn), 0.0, 1.0);
    }
    vec3 c = mix(texture(sampler2D(tex3, smp), t3).rgb, own, smoothstep(0.0, 1.0, lodw));
    c = mix(c, ramp((elev - heat.x) * heat.y), heat.z);
    if (slope.x > 0.0) {
        float cs = clamp(light, 0.0001, 1.0);
        float tn = sqrt(max(1.0 - cs * cs, 0.0)) / cs * slope.y;
        c = mix(c, slopeRamp(degrees(atan(tn)) / slope.z), slope.x);
    }
    frag_color = vec4(c * s, flags.w);
}
@end

@program globe globe_vs globe_fs
