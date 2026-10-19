@vs globe_vs
layout(binding=0) uniform globe_vs_params {
    mat4 mvp;
    vec4 uv_xform;
    vec4 uv_xform2;
};

in vec3 position;
in vec2 texcoord0;
in float shade;
in float height;

out vec2 uv;
out vec2 uv2;
out float light;
out float elev;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    uv = texcoord0 * uv_xform.xy + uv_xform.zw;
    uv2 = texcoord0 * uv_xform2.xy + uv_xform2.zw;
    light = shade;
    elev = height;
}
@end

@fs globe_fs
layout(binding=1) uniform globe_fs_params {
    vec4 flags;
    vec4 heat;
};

layout(binding=0) uniform texture2D tex;
layout(binding=1) uniform texture2D tex2;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec2 uv2;
in float light;
in float elev;
out vec4 frag_color;

vec3 ramp(float x) {
    x = clamp(x, 0.0, 1.0);
    vec3 a = mix(vec3(0.0, 0.2, 0.8), vec3(0.0, 0.75, 0.3), smoothstep(0.0, 0.25, x));
    vec3 b = mix(a, vec3(0.95, 0.9, 0.1), smoothstep(0.25, 0.5, x));
    vec3 c = mix(b, vec3(0.9, 0.4, 0.05), smoothstep(0.5, 0.75, x));
    return mix(c, vec3(0.95, 0.95, 0.95), smoothstep(0.75, 1.0, x));
}

void main() {
    vec2 t = vec2(uv.x, flags.x > 0.5 ? 1.0 - uv.y : uv.y);
    float s = mix(1.0, min(0.25 + 0.75 * light, 1.0), flags.y);
    vec2 t2 = vec2(uv2.x, flags.x > 0.5 ? 1.0 - uv2.y : uv2.y);
    vec3 c = mix(texture(sampler2D(tex2, smp), t2).rgb, texture(sampler2D(tex, smp), t).rgb, flags.z);
    c = mix(c, ramp((elev - heat.x) * heat.y), heat.z);
    frag_color = vec4(c * s, 1.0);
}
@end

@program globe globe_vs globe_fs
