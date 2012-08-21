float4x4 WorldViewProj;

texture Base  <
	string UIName =  "Base Texture";
	string ResourceType = "2D";
>;

sampler2D BaseTexture = sampler_state {
	Texture = <Base>;
	AddressU = Wrap;
	AddressV = Wrap;
};

texture Overlay  <
	string UIName =  "Overlay Texture";
	string ResourceType = "2D";
>;

sampler2D OverlayTexture = sampler_state {
	Texture = <Overlay>;
	AddressU = Wrap;
	AddressV = Wrap;
};


texture PreRender : RENDERCOLORTARGET
	<
	string Format = "X8R8G8B8" ;
	>;

sampler2D PreRenderSampler = sampler_state {
	Texture = <PreRender>;
};

struct VS_INPUT 
{
	float4 Position : POSITION0;
	float2 Tex      : TEXCOORD0;

};

struct VS_OUTPUT 
{
	float4 Position : POSITION0;
	float2 Tex      : TEXCOORD0;

};

VS_OUTPUT cap_mainVS(VS_INPUT Input)
{
	VS_OUTPUT Output;

	Output.Position = mul( Input.Position, WorldViewProj );
	Output.Tex = Input.Tex;

	return( Output );
}

float4 cap_mainPS(float2 tex: TEXCOORD0) : COLOR
{
	return tex2D(BaseTexture, tex);
}

///////////////////////////////////////////////////////

struct Overlay_VS_INPUT 
{
	float4 Position : POSITION0;
	float2 Texture1 : TEXCOORD0;

};

struct Overlay_VS_OUTPUT 
{
	float4 Position : POSITION0;
	float2 Texture1 : TEXCOORD0;
	float2 Texture2 : TEXCOORD1;

};

vector blend(vector bottom, vector top)
{
	//Overlay
	/*float r = (top.r + bottom.r <1)? (0) : (top.r + bottom.r - 1);
	float g = (top.g + bottom.g <1)? (0) : (top.g + bottom.g - 1);
	float b = (top.b + bottom.b <1)? (0) : (top.b + bottom.b - 1);

	return  vector(r,g,b,bottom.a);*/

	//Classic color burn
	//return vector(bottom.rgb + top.rgb - 1, bottom.a);

	//Color burn
	//return vector( 1 - (1-bottom.rgb)/top.rgb, bottom.a);

	//Linear light
	float r = (top.r < 0.5)? (bottom.r + 2*(top.r - 0.5)) : (bottom.r + 2*top.r - 1);
	float g = (top.g < 0.5)? (bottom.g + 2*(top.g - 0.5)) : (bottom.g + 2*top.g - 1);
	float b = (top.b < 0.5)? (bottom.b + 2*(top.b - 0.5)) : (bottom.b + 2*top.b - 1);

	return  vector(r,g,b,bottom.a);
}

Overlay_VS_OUTPUT over_mainVS(Overlay_VS_INPUT Input)
{
	Overlay_VS_OUTPUT Output;

	Output.Position = mul( Input.Position, WorldViewProj );
	Output.Texture1 = Input.Texture1;
	Output.Texture2 = Output.Position.xy*float2(0.5,0.5) + float2(0.5,0.5);

	return( Output );
}

float4 over_mainPS(float2 tex :TEXCOORD0, float2 pos :TEXCOORD1) : COLOR 
{
	vector tmp = blend(tex2D(OverlayTexture, pos), tex2D(PreRenderSampler, tex));
	return tmp;
}


technique technique0 
{		
	pass p0 
	{
		CullMode = None;
		VertexShader = compile vs_2_0 cap_mainVS();
		PixelShader = compile ps_2_0 cap_mainPS();
	}

	pass p0 
	{
		CullMode = None;
		VertexShader = compile vs_2_0 over_mainVS();
		PixelShader = compile ps_2_0 over_mainPS();
	}
}
