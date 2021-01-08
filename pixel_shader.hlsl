struct PixelInput
{
	float4 position_out: SV_POSITION;
    float2 texcoord_out: TEXCOORD;
};


cbuffer ConfigBuffer : register(b0) {
    float2 offset;
}

Texture2D tex : register(t0);
Texture2D palette_tex : register(t1);
SamplerState tex_sampler : register(s0);

float4 main(PixelInput input) : SV_TARGET {
    float density = tex.Sample(tex_sampler, input.texcoord_out + offset).r;
    return palette_tex.Sample(tex_sampler, float2(density, 0.5f));
}