@vs fsurf_vs
in vec2 position;
in vec4 color0;

out vec4 color;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    color = color0;
}
@end

@fs fsurf_fs
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = color;
}
@end

@program fsurf fsurf_vs fsurf_fs

@vs sprite_vs
in vec3 position;
in vec2 texcoord0;
in vec4 color0;

out vec2 uv;
out vec4 color;

void main() {
    gl_Position = vec4(position, 1.0);
    uv = texcoord0;
    color = color0;
}
@end

@fs sprite_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    vec4 t = texture(sampler2D(tex, smp), uv);
    frag_color = vec4(mix(color.rgb * t.r, vec3(1.0), t.g), t.a * color.a);
}
@end

@program sprite sprite_vs sprite_fs

@fs text_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    float d = texture(sampler2D(tex, smp), uv).r;
    float w = max(fwidth(d), 0.02);
    float fill = smoothstep(0.5 - w, 0.5 + w, d);
    float halo = smoothstep(0.30 - w, 0.30 + w, d);
    vec3 rgb = mix(vec3(0.0), color.rgb, fill);
    frag_color = vec4(rgb, halo * color.a);
}
@end

@program text sprite_vs text_fs

@fs line3d_fs
in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = color;
}
@end

@program line3d sprite_vs line3d_fs

@fs image_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * color;
}
@end

@program image sprite_vs image_fs
