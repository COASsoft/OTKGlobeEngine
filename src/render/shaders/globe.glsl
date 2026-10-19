@vs globe_vs
layout(binding=0) uniform globe_vs_params {
    mat4 mvp;
    vec4 uv_xform;
};

in vec3 position;
in vec2 texcoord0;

out vec2 uv;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    uv = texcoord0 * uv_xform.xy + uv_xform.zw;
}
@end

@fs globe_fs
layout(binding=1) uniform globe_fs_params {
    vec4 flags;
};

layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    vec2 t = vec2(uv.x, flags.x > 0.5 ? 1.0 - uv.y : uv.y);
    frag_color = vec4(texture(sampler2D(tex, smp), t).rgb, 1.0);
}
@end

@program globe globe_vs globe_fs
