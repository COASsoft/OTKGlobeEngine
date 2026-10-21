@vs bline_vs
layout(binding=0) uniform bline_params {
    vec4 xform;
    vec4 frame;
    vec4 width;
    vec4 dash_on;
    vec4 dash_off;
    vec4 color0;
    vec4 color1;
    vec4 color2;
    vec4 color3;
};

in vec2 corner;
in vec4 seg;
in vec2 info;

out vec4 color;
out vec3 dist;
out vec2 dash;

void main() {
    vec4 sel = vec4(equal(vec4(floor(info.y + 0.5)), vec4(0.0, 1.0, 2.0, 3.0)));
    float w = dot(sel, width) * frame.z;
    vec2 a = vec2(seg.x * frame.x, seg.y);
    vec2 b = vec2(seg.z * frame.x, seg.w);
    vec2 d = b - a;
    float len = length(d);
    vec2 dir = len > 0.0 ? d / len : vec2(1.0, 0.0);
    vec2 n = vec2(-dir.y, dir.x);
    float half_px = w * 0.5 + 1.0;
    vec2 p = mix(a, b, corner.x) + n * (corner.y * half_px * frame.y);
    p.x /= frame.x;
    gl_Position = w > 0.0 ? vec4(p * xform.xy + xform.zw, 0.0, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
    color = sel.x * color0 + sel.y * color1 + sel.z * color2 + sel.w * color3;
    dist = vec3(corner.y * half_px, (info.x + corner.x * len) / frame.y, w * 0.5);
    dash = vec2(dot(sel, dash_on), dot(sel, dash_off)) * frame.z;
}
@end

@fs bline_fs
in vec4 color;
in vec3 dist;
in vec2 dash;
out vec4 frag_color;

void main() {
    float a = clamp(dist.z + 0.5 - abs(dist.x), 0.0, 1.0);
    if (dash.x > 0.0) {
        float t = mod(dist.y, dash.x + dash.y);
        a *= clamp(min(t, dash.x - t) + 0.5, 0.0, 1.0);
    }
    frag_color = vec4(color.rgb, color.a * a);
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
    float d = texture(sampler2D(tex, smp), uv).r;
    float w = max(fwidth(d), 1e-4);
    float px = (d - 0.5 + params.y) / w;
    float a = clamp(px + 0.5, 0.0, 1.0);
    float h = clamp(px + params.x + 0.5, 0.0, 1.0);
    float alpha = mix(halo.a * h, fill.a, a);
    vec3 rgb = mix(halo.rgb, fill.rgb, a);
    frag_color = vec4(rgb, alpha);
}
@end

@program label label_vs label_fs
