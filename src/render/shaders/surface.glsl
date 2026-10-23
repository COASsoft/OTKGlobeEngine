@vs surface_vs
in vec2 position;
in vec2 texcoord0;

out vec2 uv;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = texcoord0;
}
@end

@fs surface_fs
layout(binding=1) uniform surface_fs_params {
    vec4 tint;
};

layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    vec4 c = texture(sampler2D(tex, smp), uv);
    frag_color = vec4(c.rgb * tint.rgb, c.a * tint.a);
}
@end

@program surface surface_vs surface_fs
