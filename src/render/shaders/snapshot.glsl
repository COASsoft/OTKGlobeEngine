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
};

layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    vec2 t = vec2(uv.x, params.y > 0.5 ? 1.0 - uv.y : uv.y);
    frag_color = vec4(texture(sampler2D(tex, smp), t).rgb, params.x);
}
@end

@program snapshot snapshot_vs snapshot_fs
