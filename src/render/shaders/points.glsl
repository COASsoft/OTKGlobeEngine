@vs points_vs
layout(binding=0) uniform points_params {
    mat4 mvp;
    vec4 size;
};

in vec2 corner;
in vec3 position;
in vec4 color0;

out vec4 color;

void main() {
    vec4 c = mvp * vec4(position, 1.0);
    c.xy += corner * size.xy * c.w;
    gl_Position = c;
    color = color0;
}
@end

@fs points_fs
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = color;
}
@end

@program points points_vs points_fs
