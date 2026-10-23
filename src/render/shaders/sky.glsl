@vs sky_vs
in vec2 position;

out vec2 ndc;

void main() {
    gl_Position = vec4(position, 1.0, 1.0);
    ndc = position;
}
@end

@fs sky_fs
layout(binding=1) uniform sky_fs_params {
    mat4 inv_vp;
    vec4 center;
    vec4 radii;
};

in vec2 ndc;
out vec4 frag_color;

bool sphere(vec3 o, vec3 d, vec3 c, float r, out float t0, out float t1) {
    vec3 oc = o - c;
    float b = dot(oc, d);
    float h = length(oc - b * d);
    float disc = (r - h) * (r + h);
    if (disc < 0.0) {
        t0 = 0.0;
        t1 = 0.0;
        return false;
    }
    float s = sqrt(disc);
    t0 = -b - s;
    t1 = -b + s;
    return t1 > 0.0;
}

void main() {
    vec4 p = inv_vp * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w);
    vec3 c = center.xyz;
    float a0, a1, e0, e1;
    if (!sphere(vec3(0.0), dir, c, radii.y, a0, a1)) {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    a0 = max(a0, 0.0);
    bool ground = sphere(vec3(0.0), dir, c, radii.x, e0, e1) && e0 > 0.0;
    if (ground && radii.w > 0.5) {
        frag_color = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }
    if (ground) a1 = min(a1, e0);
    float len = max(a1 - a0, 0.0);
    vec3 mid = dir * (0.5 * (a0 + a1)) - c;
    float mu = dot(dir, normalize(mid));
    float inside = radii.z;
    vec3 zenith = vec3(0.30, 0.52, 0.78);
    vec3 horizon = vec3(0.76, 0.85, 0.92);
    vec3 col = mix(horizon, zenith, clamp(abs(mu) * 2.5, 0.0, 1.0));
    float density = 1.0 - exp(-len / (inside > 0.5 ? 60000.0 : 350000.0));
    frag_color = vec4(col * density, 1.0);
}
@end

@program sky sky_vs sky_fs
