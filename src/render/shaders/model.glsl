@vs model_vs
layout(binding=0) uniform model_vs_params {
    mat4 mvp;
    mat4 rotation;
};

in vec3 position;
in vec3 normal;
in vec2 texcoord0;

out vec3 n;
out vec2 uv;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    n = (rotation * vec4(normal, 0.0)).xyz;
    uv = texcoord0;
}
@end

@fs model_fs
layout(binding=1) uniform model_fs_params {
    vec4 base_color;
    vec4 light;
};

layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec3 n;
in vec2 uv;
out vec4 frag_color;

void main() {
    vec4 c = base_color * texture(sampler2D(tex, smp), uv);
    float d = abs(dot(normalize(n), normalize(light.xyz)));
    frag_color = vec4(c.rgb * (0.35 + 0.65 * d), c.a);
}
@end

@program model model_vs model_fs
