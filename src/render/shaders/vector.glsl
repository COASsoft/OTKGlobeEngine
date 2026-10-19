@vs vfill_vs
layout(binding=0) uniform vector_params {
    vec4 xform;
    vec4 line;
};

in vec2 position;
in vec4 color0;

out vec4 color;

void main() {
    gl_Position = vec4(position * xform.xy + xform.zw, 0.0, 1.0);
    color = color0;
}
@end

@fs vfill_fs
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = color;
}
@end

@program vfill vfill_vs vfill_fs

@vs vline_vs
layout(binding=0) uniform vector_params {
    vec4 xform;
    vec4 line;
};

in vec2 position;
in vec2 other;
in vec2 extra;
in vec4 color0;

out vec4 color;

void main() {
    vec2 a = vec2(position.x * line.x, position.y);
    vec2 b = vec2(other.x * line.x, other.y);
    vec2 d = b - a;
    float len = max(length(d), 1e-12);
    vec2 n = vec2(-d.y, d.x) / len;
    vec2 e = d / len;
    float half_width = extra.y * line.y;
    vec2 p = a + n * extra.x * half_width - e * half_width * sign(extra.x * 0.0 + 1.0) * line.z;
    p.x /= line.x;
    gl_Position = vec4(p * xform.xy + xform.zw, 0.0, 1.0);
    color = color0;
}
@end

@program vline vline_vs vfill_fs
