@vs mip_vs
in vec2 position;

out vec2 uv;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = position * 0.5 + 0.5;
}
@end

@fs mip_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    vec2 t = vec2(uv.x, 1.0 - uv.y);
    frag_color = texture(sampler2D(tex, smp), t);
}
@end

@program mip mip_vs mip_fs
