@vs model_vs
layout(binding=0) uniform model_vs_params {
    mat4 view_proj;
    mat4 world;
    mat4 rotation;
};

in vec3 position;
in vec3 normal;
in vec2 texcoord0;

out vec3 n;
out vec2 uv;
out vec3 wpos;

void main() {
    vec4 w = world * vec4(position, 1.0);
    gl_Position = view_proj * w;
    wpos = w.xyz;
    n = (rotation * vec4(normal, 0.0)).xyz;
    uv = texcoord0;
}
@end

@vs model_inst_vs
layout(binding=0) uniform model_inst_params {
    mat4 view_proj;
    mat4 world;
    mat4 rotation;
    mat4 node;
};

in vec3 position;
in vec3 normal;
in vec2 texcoord0;
in vec4 inst0;
in vec4 inst1;
in vec4 inst2;
in vec4 inst3;

out vec3 n;
out vec2 uv;
out vec3 wpos;

void main() {
    mat4 inst = mat4(inst0, inst1, inst2, inst3);
    vec4 w = world * (inst * (node * vec4(position, 1.0)));
    gl_Position = view_proj * w;
    wpos = w.xyz;
    n = (rotation * (inst * (node * vec4(normal, 0.0)))).xyz;
    uv = texcoord0;
}
@end

@fs model_fs
layout(binding=1) uniform model_fs_params {
    vec4 base_color;
    vec4 light;
    vec4 emissive;
    vec4 material;
    vec4 sky;
};

layout(binding=0) uniform texture2D tex;
layout(binding=1) uniform texture2D nrm_tex;
layout(binding=2) uniform texture2D emi_tex;
layout(binding=3) uniform texture2D orm_tex;
layout(binding=0) uniform sampler smp;

in vec3 n;
in vec2 uv;
in vec3 wpos;
out vec4 frag_color;

void main() {
    vec4 base = base_color * texture(sampler2D(tex, smp), uv);
    if (emissive.w > 0.5) {
        frag_color = base;
        return;
    }
    vec3 V = normalize(-wpos);
    vec3 N = dot(n, n) > 0.25 ? normalize(n) : normalize(cross(dFdx(wpos), dFdy(wpos)));
    if (dot(N, V) < 0.0) N = -N;
    if (material.z > 0.0) {
        vec3 dp1 = dFdx(wpos);
        vec3 dp2 = dFdy(wpos);
        vec2 duv1 = dFdx(uv);
        vec2 duv2 = dFdy(uv);
        vec3 dp2perp = cross(dp2, N);
        vec3 dp1perp = cross(N, dp1);
        vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
        vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
        float len = max(dot(T, T), dot(B, B));
        if (len > 0.0) {
            float inv = inversesqrt(len);
            vec3 tn = texture(sampler2D(nrm_tex, smp), uv).xyz * 2.0 - 1.0;
            tn.xy *= material.z;
            N = normalize(mat3(T * inv, B * inv, N) * tn);
        }
    }
    vec4 orm = texture(sampler2D(orm_tex, smp), uv);
    orm.r = mix(1.0, orm.r, sky.w);
    float rough = clamp(material.y * orm.g, 0.05, 1.0);
    float metal = clamp(material.x * orm.b, 0.0, 1.0);
    vec3 albedo = pow(base.rgb, vec3(2.2));
    vec3 L = normalize(light.xyz);
    vec3 H = normalize(L + V);
    float ndl = max(dot(N, L), 0.0);
    float a2 = rough * rough * rough * rough;
    float shininess = max(2.0 / max(a2, 0.0001) - 2.0, 1.0);
    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    float spec = pow(max(dot(N, H), 0.0), shininess) * (shininess + 8.0) / 25.13;
    float hemi = mix(0.6, 1.0, dot(N, normalize(sky.xyz)) * 0.5 + 0.5);
    vec3 diffuse = albedo * (1.0 - metal);
    vec3 color = diffuse * (material.w * hemi * orm.r + light.w * ndl) + fresnel * spec * light.w * ndl;
    color += albedo * metal * 0.8 * (material.w * hemi * orm.r + light.w * ndl);
    color += emissive.rgb * pow(texture(sampler2D(emi_tex, smp), uv).rgb, vec3(2.2));
    frag_color = vec4(pow(color, vec3(1.0 / 2.2)), base.a);
}
@end

@program model model_vs model_fs
@program model_inst model_inst_vs model_fs
