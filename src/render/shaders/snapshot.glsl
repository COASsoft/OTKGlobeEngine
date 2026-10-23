@vs snapshot_vs
in vec2 position;

out vec2 uv;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = position * 0.5 + 0.5;
}
@end

@fs snapshot_fs
layout(binding=1) uniform snapshot_params {
    vec4 params;
    vec4 h0;
    vec4 h1;
    vec4 h2;
};

layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    vec3 n = vec3(uv * 2.0 - 1.0, 1.0);
    vec3 s = vec3(dot(h0.xyz, n), dot(h1.xyz, n), dot(h2.xyz, n));
    vec2 su = s.xy / max(abs(s.z), 1.0e-6) * 0.5 + 0.5;
    float inside = (s.z > 0.0 && su.x >= 0.0 && su.x <= 1.0 && su.y >= 0.0 && su.y <= 1.0) ? 1.0 : 0.0;
    vec2 t = vec2(su.x, params.y > 0.5 ? 1.0 - su.y : su.y);
    frag_color = vec4(texture(sampler2D(tex, smp), t).rgb, params.x * inside);
}
@end

@program snapshot snapshot_vs snapshot_fs
