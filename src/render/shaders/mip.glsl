@vs mip_vs
in vec2 position;

out vec2 uv;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = position * 0.5 + 0.5;
}
@end

@fs mip_fs
layout(binding=1) uniform mip_params {
    vec4 params;
};

layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    vec2 t = vec2(uv.x, params.x > 0.5 ? 1.0 - uv.y : uv.y);
    float block = params.y;
    int n = int(params.z);
    float texel = params.w;
    float stride = block / params.z;
    vec2 origin = t - vec2(block * texel * 0.5);
    vec4 sum = vec4(0.0);
    for (int j = 0; j < 16; j++) {
        if (j >= n) break;
        for (int i = 0; i < 16; i++) {
            if (i >= n) break;
            sum += texture(sampler2D(tex, smp), origin + (vec2(float(i), float(j)) + 0.5) * stride * texel);
        }
    }
    frag_color = sum / float(n * n);
}
@end

@program mip mip_vs mip_fs
