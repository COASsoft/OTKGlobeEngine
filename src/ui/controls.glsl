@vs controls_vs
layout(binding=0) uniform controls_params {
    vec4 screen;
};

in vec2 position;
in vec2 local;
in vec4 shape_a;
in vec4 shape_b;
in vec4 color0;
in vec4 color1;
in vec2 misc;

out vec2 p;
out vec4 sa;
out vec4 sb;
out vec4 fill;
out vec4 edge;
out vec2 m;

void main() {
    gl_Position = vec4(position.x * screen.x - 1.0, 1.0 - position.y * screen.y, 0.0, 1.0);
    p = local;
    sa = shape_a;
    sb = shape_b;
    fill = color0;
    edge = color1;
    m = misc;
}
@end

@fs controls_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 p;
in vec4 sa;
in vec4 sb;
in vec4 fill;
in vec4 edge;
in vec2 m;
out vec4 frag_color;

float box(vec2 q, vec2 b, float r) {
    vec2 d = abs(q) - b + vec2(r);
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

float segment(vec2 q, vec2 a, vec2 b) {
    vec2 pa = q - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

float triangle(vec2 q, vec2 p0, vec2 p1, vec2 p2) {
    vec2 e0 = p1 - p0;
    vec2 e1 = p2 - p1;
    vec2 e2 = p0 - p2;
    vec2 v0 = q - p0;
    vec2 v1 = q - p1;
    vec2 v2 = q - p2;
    vec2 q0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 q1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 q2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(q0, q0), s * (v0.x * e0.y - v0.y * e0.x)), vec2(dot(q1, q1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(q2, q2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

void main() {
    float kind = m.x;
    if (kind < 0.5) {
        float a = texture(sampler2D(tex, smp), p).r;
        frag_color = vec4(fill.rgb, fill.a * a);
        return;
    }
    if (kind > 3.5) {
        float s = box(p, sa.xy, sa.z);
        frag_color = vec4(fill.rgb, fill.a * (1.0 - smoothstep(-sa.w, sa.w, s)));
        return;
    }
    float d;
    if (kind < 1.5) {
        d = box(p, sa.xy, sa.z);
    } else if (kind < 2.5) {
        d = segment(p, sa.xy, sa.zw) - sb.x;
    } else {
        d = triangle(p, sa.xy, sa.zw, sb.xy);
    }
    float cover = clamp(0.5 - d, 0.0, 1.0);
    float w = m.y;
    vec4 c = fill;
    if (w > 0.0) {
        float inner = clamp(0.5 - d - w, 0.0, 1.0);
        c = mix(edge, fill, cover > 0.0 ? inner / cover : 0.0);
    }
    frag_color = vec4(c.rgb, c.a * cover);
}
@end

@program controls controls_vs controls_fs
