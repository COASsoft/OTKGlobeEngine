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
    mat4 inv_new;
    mat4 vp_old;
    vec4 delta;
    vec4 up;
    vec4 shape;
};

layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    vec2 t = uv;
    float inside = 1.0;
    if (params.z > 0.5) {
        vec2 ndc = uv * 2.0 - 1.0;
        vec4 a = inv_new * vec4(ndc, 0.0, 1.0);
        vec4 b = inv_new * vec4(ndc, 1.0, 1.0);
        vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
        vec3 ds = normalize(vec3(dir.xy, dir.z * shape.y));
        float bb = up.w * dot(up.xyz, ds);
        float cc = shape.z * (2.0 * shape.x + shape.z);
        float disc = bb * bb - cc;
        float hit = disc >= 0.0 ? -bb - sqrt(disc) : -1.0;
        vec4 clip;
        if (hit > 0.0) {
            vec3 rel = vec3(ds.xy * hit, ds.z * hit / shape.y);
            clip = vp_old * vec4(rel + delta.xyz, 1.0);
        } else {
            clip = vp_old * vec4(dir, 0.0);
        }
        if (clip.w <= 0.0) inside = 0.0;
        t = clip.xy / max(abs(clip.w), 1.0e-9) * 0.5 + 0.5;
        if (t.x < 0.0 || t.x > 1.0 || t.y < 0.0 || t.y > 1.0) inside = 0.0;
    }
    vec2 s = vec2(t.x, params.y > 0.5 ? 1.0 - t.y : t.y);
    frag_color = vec4(texture(sampler2D(tex, smp), s).rgb, params.x * inside);
}
@end

@program snapshot snapshot_vs snapshot_fs
