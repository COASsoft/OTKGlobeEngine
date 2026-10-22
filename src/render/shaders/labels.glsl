@vs bline_vs
layout(binding=0) uniform bline_params {
    vec4 xform;
    vec4 frame;
    vec4 shade;
    vec4 styles[16];
    vec4 colors[16];
};

in vec2 corner;
in vec4 seg;
in vec2 info;

out vec4 tint;
out vec4 edge;
out vec4 dist;
out vec3 dash;

void main() {
    int s = int(clamp(floor(info.y + 0.5), 0.0, 15.0));
    vec4 style = styles[s];
    float w = style.x * frame.z;
    float c = style.y * frame.z;
    vec2 a = vec2(seg.x * frame.x, seg.y);
    vec2 b = vec2(seg.z * frame.x, seg.w);
    vec2 d = b - a;
    float len = length(d);
    vec2 dir = len > 0.0 ? d / len : vec2(1.0, 0.0);
    vec2 n = vec2(-dir.y, dir.x);
    float half_px = w * 0.5 + c + 1.0;
    vec2 p = mix(a, b, corner.x) + n * (corner.y * half_px * frame.y);
    p.x /= frame.x;
    gl_Position = w > 0.0 ? vec4(p * xform.xy + xform.zw, 0.0, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
    tint = colors[s];
    edge = shade;
    dist = vec4(corner.y * half_px, (info.x + corner.x * len) / frame.y, w * 0.5, c);
    dash = vec3(style.z * frame.z, style.w * frame.z, frame.w);
}
@end

@fs bline_fs
in vec4 tint;
in vec4 edge;
in vec4 dist;
in vec3 dash;
out vec4 frag_color;

void main() {
    float d = abs(dist.x);
    float inner = clamp(dist.z + 0.5 - d, 0.0, 1.0);
    float outer = dist.w > 0.0 ? clamp(dist.z + dist.w + 0.5 - d, 0.0, 1.0) : 0.0;
    vec3 rgb;
    float a;
    if (dash.z < 0.5) {
        rgb = edge.rgb;
        a = edge.a * outer;
    } else if (dash.z < 1.5) {
        rgb = tint.rgb;
        a = tint.a * inner;
    } else {
        float fill = tint.a * inner;
        float ring = edge.a * outer;
        a = fill + ring * (1.0 - fill);
        rgb = a > 0.0 ? (tint.rgb * fill + edge.rgb * ring * (1.0 - fill)) / a : tint.rgb;
    }
    if (dash.x > 0.0) {
        float t = mod(dist.y, dash.x + dash.y);
        a *= clamp(min(t, dash.x - t) + 0.5, 0.0, 1.0);
    }
    frag_color = vec4(rgb, a);
}
@end

@program bline bline_vs bline_fs

@vs label_vs
in vec2 position;
in vec2 texcoord0;
in vec4 color0;
in vec4 color1;
in vec2 texcoord1;

out vec2 uv;
out vec4 fill;
out vec4 halo;
out vec2 params;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = texcoord0;
    fill = color0;
    halo = color1;
    params = texcoord1;
}
@end

@fs label_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 fill;
in vec4 halo;
in vec2 params;
out vec4 frag_color;

void main() {
    vec2 cover = texture(sampler2D(tex, smp), uv).rg;
    float inner = fill.a * cover.r;
    float outer = halo.a * cover.g;
    float a = inner + outer * (1.0 - inner);
    vec3 rgb = a > 0.0 ? (fill.rgb * inner + halo.rgb * outer * (1.0 - inner)) / a : fill.rgb;
    frag_color = vec4(rgb, a);
}
@end

@program label label_vs label_fs

@fs icon_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 fill;
in vec4 halo;
in vec2 params;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * fill;
}
@end

@program icon label_vs icon_fs
