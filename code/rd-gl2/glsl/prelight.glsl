/*[Vertex]*/
#if defined(POINT_LIGHT) || defined(CUBEMAP) 
#define USE_VOLUME_SPHERE
#endif

#if defined(USE_VOLUME_SPHERE)
in vec3 in_Position;
uniform mat4 u_ModelViewProjectionMatrix;
uniform vec3 u_ViewOrigin;
#endif

#if defined(POINT_LIGHT)
uniform vec4 u_LightTransforms[16]; // xyz = position, w = scale
uniform vec3 u_LightColors[16];
flat out vec4 var_Position;
flat out vec3 var_LightColor;
#endif

#if defined(CUBEMAP)
uniform vec4 u_CubemapTransforms[16]; // xyz = position, w = scale
flat out vec4 var_Position;
flat out int  var_Index;
#endif

uniform vec3 u_ViewForward;
uniform vec3 u_ViewLeft;
uniform vec3 u_ViewUp;
uniform int  u_VertOffset;

out vec3 var_ViewDir;
flat out int var_Instance;

void main()
{
	var_Instance			= gl_InstanceID;
#if defined(POINT_LIGHT)
	var_Position			= u_LightTransforms[gl_InstanceID + u_VertOffset];
	var_LightColor			= u_LightColors[gl_InstanceID + u_VertOffset];
	var_LightColor			*= var_LightColor;
	var_LightColor			*= var_Position.w;
#elif defined(CUBEMAP)
	var_Index				= gl_InstanceID + u_VertOffset;
	var_Position			= u_CubemapTransforms[gl_InstanceID + u_VertOffset];
#endif

#if defined(USE_VOLUME_SPHERE)
	vec3 worldSpacePosition = in_Position * var_Position.w * 1.1 + var_Position.xyz;
	gl_Position				= u_ModelViewProjectionMatrix * vec4(worldSpacePosition, 1.0);
	var_ViewDir				= normalize(worldSpacePosition - u_ViewOrigin);
	//vec2 screenCoords		= gl_Position.xy / gl_Position.w;
	//var_ViewDir				= (u_ViewForward + u_ViewLeft * -screenCoords.x) + u_ViewUp * screenCoords.y;
#else
	vec2 position			= vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position				= vec4(position, 0.0, 1.0);
	var_ViewDir				= (u_ViewForward + u_ViewLeft * -position.x) + u_ViewUp * position.y;
#endif
}

/*[Fragment]*/

uniform vec3 u_ViewOrigin;
uniform vec4 u_ViewInfo; // zfar / znear, zfar, znear
#define u_ZFarDivZNear u_ViewInfo.x
#define u_ZFar u_ViewInfo.y
#define u_ZNear u_ViewInfo.z
uniform sampler2D u_ScreenDepthMap;
uniform sampler2D u_NormalMap;
uniform sampler2D u_SpecularMap;

uniform mat4 u_ModelViewProjectionMatrix;
uniform mat4 u_InvViewProjectionMatrix;

#if defined(POINT_LIGHT)
uniform sampler3D u_LightGridDirectionMap;
uniform sampler3D u_LightGridDirectionalLightMap;
uniform sampler3D u_LightGridAmbientLightMap;
uniform vec3 u_LightGridOrigin;
uniform vec3 u_LightGridCellInverseSize;
uniform vec3 u_StyleColor;
uniform vec2 u_LightGridLightScale;
uniform vec3 u_ViewForward;
uniform vec3 u_ViewLeft;
uniform vec3 u_ViewUp;
uniform int u_VertOffset;

uniform samplerCubeShadow u_ShadowMap;
uniform samplerCubeShadow u_ShadowMap2;
uniform samplerCubeShadow u_ShadowMap3;
uniform samplerCubeShadow u_ShadowMap4;

#define u_LightGridAmbientScale u_LightGridLightScale.x
#define u_LightGridDirectionalScale u_LightGridLightScale.y
#endif

#if defined(SUN_LIGHT)
uniform vec3 u_ViewForward;
uniform vec3 u_ViewLeft;
uniform vec3 u_ViewUp;
uniform vec4 u_PrimaryLightOrigin;
uniform vec3 u_PrimaryLightColor;
uniform vec3 u_PrimaryLightAmbient;
uniform float u_PrimaryLightRadius;
uniform sampler2D u_ShadowMap;
#endif

#if defined(CUBEMAP)
uniform samplerCube u_ShadowMap;
uniform samplerCube u_ShadowMap2;
uniform samplerCube u_ShadowMap3;
uniform samplerCube u_ShadowMap4;
uniform sampler2D	u_EnvBrdfMap;
uniform vec4		u_CubeMapInfo;
uniform vec4		u_CubemapTransforms[16]; // xyz = position, w = scale
uniform int			u_NumCubemaps;
flat in int			var_Index;
#endif

in vec3 var_ViewDir;
flat in int  var_Instance;

#if defined(POINT_LIGHT)
in vec2 var_screenCoords;
flat in vec4 var_Position;
flat in vec3 var_LightColor;
#endif

out vec4 out_Color;
out vec4 out_Glow;

float LinearDepth(float zBufferDepth, float zFarDivZNear)
{
	return 1.0 / mix(zFarDivZNear, 1.0, zBufferDepth);
}

vec3 WorldPosFromDepth(float depth, vec2 TexCoord) {
	//mat4 inverseProjectionView = inverse(u_ModelViewProjectionMatrix);

    float z = depth * 2.0 - 1.0;

    vec4 clipSpacePosition = vec4(TexCoord * 2.0 - 1.0, z, 1.0);
    vec4 worldPosition = u_InvViewProjectionMatrix * clipSpacePosition;
	worldPosition = vec4((worldPosition.xyz / worldPosition.w ), 1.0f);

    return worldPosition.xyz;
}

vec3 DecodeNormal(in vec2 N)
{
	vec2 encoded = N*4.0 - 2.0;
	float f = dot(encoded, encoded);
	float g = sqrt(1.0 - f * 0.25);

	return vec3(encoded * g, 1.0 - f * 0.5);
}

float spec_D(
	float NH,
	float roughness)
{
	// normal distribution
	// from http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
	float alpha = roughness * roughness;
	float quotient = alpha / max(1e-8, (NH*NH*(alpha*alpha - 1.0) + 1.0));
	return (quotient * quotient) / M_PI;
}

vec3 spec_F(
	float EH,
	vec3 F0)
{
	// Fresnel
	// from http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
	float pow2 = pow(2.0, (-5.55473*EH - 6.98316) * EH);
	return F0 + (vec3(1.0) - F0) * pow2;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float G1(
	float NV,
	float k)
{
	return NV / (NV*(1.0 - k) + k);
}

float spec_G(float NL, float NE, float roughness)
{
	// GXX Schlick
	// from http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
	float k = max(((roughness + 1.0) * (roughness + 1.0)) / 8.0, 1e-5);
	return G1(NL, k)*G1(NE, k);
}

vec3 CalcSpecular(
	in vec3 specular,
	in float NH,
	in float NL,
	in float NE,
	in float EH,
	in float roughness
	)
{
	float distrib = spec_D(NH, roughness);
	vec3 fresnel = spec_F(EH, specular);
	float vis = spec_G(NL, NE, roughness);
	return (distrib * fresnel * vis);
}

#if defined(POINT_LIGHT)

float CalcLightAttenuation(float distance, float radius)
{
	float d = pow(distance / radius, 4.0);
	float attenuation = clamp(1.0 - d, 0.0, 1.0);
	attenuation *= attenuation;
	attenuation /= distance * distance + 1.0;

	return clamp(attenuation, 0.0, 1.0);
}

#define DEPTH_MAX_ERROR 0.000000059604644775390625

vec3 sampleOffsetDirections[20] = vec3[]
(
	vec3(1, 1, 1), vec3(1, -1, 1), vec3(-1, -1, 1), vec3(-1, 1, 1),
	vec3(1, 1, -1), vec3(1, -1, -1), vec3(-1, -1, -1), vec3(-1, 1, -1),
	vec3(1, 1, 0), vec3(1, -1, 0), vec3(-1, -1, 0), vec3(-1, 1, 0),
	vec3(1, 0, 1), vec3(-1, 0, 1), vec3(1, 0, -1), vec3(-1, 0, -1),
	vec3(0, 1, 1), vec3(0, -1, 1), vec3(0, -1, -1), vec3(0, 1, -1)
	);

float pcfShadow(samplerCubeShadow depthMap, vec3 L, float distance)
{
	float shadow = 0.0;
	int samples = 20;
	float diskRadius = 128.0/512.0;
	for (int i = 0; i < samples; ++i)
	{
		shadow += texture(depthMap, vec4(L + sampleOffsetDirections[i] * diskRadius, distance));
	}
	shadow /= float(samples);
	return shadow;
}

float getLightDepth(vec3 Vec, float f)
{
	vec3 AbsVec = abs(Vec);
	float Z = max(AbsVec.x, max(AbsVec.y, AbsVec.z));

	const float n = 1.0;

	float NormZComp = (f + n) / (f - n) - 2 * f*n / (Z* (f - n));

	return ((NormZComp + 1.0) * 0.5) - DEPTH_MAX_ERROR;
}

float getShadowValue(vec4 light)
{
	float distance = getLightDepth(light.xyz, light.w);

	if (var_Instance == 0)
		return pcfShadow(u_ShadowMap, light.xyz, distance);
	if (var_Instance == 1)
		return pcfShadow(u_ShadowMap2, light.xyz, distance);
	if (var_Instance == 2)
		return pcfShadow(u_ShadowMap3, light.xyz, distance);
	else
		return pcfShadow(u_ShadowMap4, light.xyz, distance);
}
#endif

#if defined(CUBEMAP)

float getCubemapWeight(in vec3 position)
{
	float lengthMin = 10000000.0;
	float lengthMin2 = 10000000.0;
	float lengthMin3 = 10000000.0;
	int closest, secondclosest, thirdclosest = -1;

	for (int i = 0; i < 16; i++)
	{
		vec3 dPosition = position - u_CubemapTransforms[i].xyz;
		float length = length(dPosition);
		if (length < lengthMin)
		{
			lengthMin3 = lengthMin2;
			thirdclosest = secondclosest;
			lengthMin2 = lengthMin;
			secondclosest = closest;
			lengthMin = length;
			closest = i;
		}
		else if (length < lengthMin2)
		{
			lengthMin3 = lengthMin2;
			thirdclosest = secondclosest;
			lengthMin2 = length;
			secondclosest = i;
		}
		else if (length < lengthMin3)
		{
			lengthMin3 = length;
			thirdclosest = i;
		}
	}

	float factor = 0.0;
	float sum = 0.0;

	if ( var_Index == closest)
		factor = 1.0 - pow(lengthMin / u_CubemapTransforms[closest].w, 4.0);
	else if ( var_Index == secondclosest)
		factor = 1.0 - pow(lengthMin2/ u_CubemapTransforms[secondclosest].w, 4.0);
	else if ( var_Index == thirdclosest)
		factor = 1.0 - pow(lengthMin3/ u_CubemapTransforms[thirdclosest].w, 4.0);
	else
		return 0.0;

	if (lengthMin < u_CubemapTransforms[closest].w && closest < u_NumCubemaps && closest != -1)
		sum += 1.0 - pow(lengthMin / u_CubemapTransforms[closest].w, 4.0);
	if (lengthMin2 < u_CubemapTransforms[secondclosest].w && secondclosest < u_NumCubemaps && secondclosest != -1)
		sum += 1.0 - pow(lengthMin2 / u_CubemapTransforms[secondclosest].w, 4.0);
	if (lengthMin3 < u_CubemapTransforms[thirdclosest].w && thirdclosest < u_NumCubemaps && thirdclosest != -1)
		sum += 1.0 - pow(lengthMin3 / u_CubemapTransforms[thirdclosest].w, 4.0);

	if (sum == 0.0)
		return 0.0;
	else
		return clamp(factor/sum, 0.0, 1.0);
}

#endif

void main()
{
	vec3 H;
	float NL, NH, NE, EH;
	float attenuation;

	ivec2 windowCoord = ivec2(gl_FragCoord.xy);

	vec4 specularAndGloss = texelFetch(u_SpecularMap, windowCoord, 0);
	float roughness = 1.0 - specularAndGloss.a;
	specularAndGloss.rgb *= specularAndGloss.rgb;

	//vec2 normal = texelFetch(u_NormalMap, windowCoord, 0).rg;
	vec3 normal = texelFetch(u_NormalMap, windowCoord, 0).rgb;
	float depth = texelFetch(u_ScreenDepthMap, windowCoord, 0).r;
	
	#if defined(POINT_LIGHT) || defined(CUBEMAP)
		vec3 position = WorldPosFromDepth(depth, gl_FragCoord.xy * r_FBufScale);
	#else
		float linearDepth = LinearDepth(depth, u_ZFarDivZNear);
		vec3 position = u_ViewOrigin + var_ViewDir * linearDepth;
	#endif
	
	//vec3 N = normalize(DecodeNormal(normal));
	vec3 N = normalize(normal);
	vec3 E = normalize(-var_ViewDir);
	
	vec3 diffuseOut = vec3(1.0, 0.0, 0.0);
	vec3 specularOut = vec3(0.0, 1.0, 0.0);

#if defined(POINT_LIGHT)
	vec4 lightVec		= vec4(var_Position.xyz - position, var_Position.w);
	vec3 L				= lightVec.xyz;
	float lightDist		= length(L);
	L				   /= lightDist;

	NL = clamp(dot(N, L), 1e-8, 1.0);

	attenuation  = CalcLightAttenuation(lightDist, var_Position.w);
	attenuation *= NL;

	#if defined(USE_DSHADOWS)
		attenuation *= getShadowValue(lightVec);
	#endif

	if (attenuation < 0.000001)
		discard;

	H = normalize(L + E);
	EH = max(1e-8, dot(E, H));
	NH = max(1e-8, dot(N, H));
	NE = abs(dot(N, E)) + 1e-5;

	vec3 reflectance = vec3(1.0, 1.0, 1.0);
	diffuseOut = sqrt(var_LightColor * reflectance * attenuation);

	reflectance = CalcSpecular(specularAndGloss.rgb, NH, NL, NE, EH, roughness);
	specularOut = sqrt(var_LightColor * reflectance * attenuation);
#elif defined(CUBEMAP)

	float weight = getCubemapWeight(position);

	if (weight == 0.0)
		discard;

	NE = clamp(dot(N, E), 0.0, 1.0);
	vec3 EnvBRDF = texture(u_EnvBrdfMap, vec2(roughness, NE)).rgb;

	vec3 R = reflect(E, N);

	// parallax corrected cubemap (cheaper trick)
	// from http://seblagarde.wordpress.com/2012/09/29/image-based-lighting-approaches-and-parallax-corrected-cubemap/
	vec3 parallax = u_CubeMapInfo.xyz + u_CubeMapInfo.w * -var_ViewDir;

	vec3 cubeLightColor = vec3(0.0);
	if (var_Instance == 0)
		cubeLightColor = textureLod(u_ShadowMap, R + parallax, ROUGHNESS_MIPS * roughness).rgb;
	if (var_Instance == 1)
		cubeLightColor = textureLod(u_ShadowMap2, R + parallax, ROUGHNESS_MIPS * roughness).rgb;
	if (var_Instance == 2)
		cubeLightColor = textureLod(u_ShadowMap3, R + parallax, ROUGHNESS_MIPS * roughness).rgb;
	else
		cubeLightColor = textureLod(u_ShadowMap4, R + parallax, ROUGHNESS_MIPS * roughness).rgb;

	float horiz = 1.0;
	// from http://marmosetco.tumblr.com/post/81245981087
	#if defined(HORIZON_FADE)
		const float horizonFade = HORIZON_FADE;
		horiz = clamp( 1.0 + horizonFade * dot(-R,N.xyz), 0.0, 1.0 );
		horiz *= horiz;
	#endif

    cubeLightColor *= cubeLightColor;
	diffuseOut		= sqrt(cubeLightColor * (specularAndGloss.rgb * EnvBRDF.x + EnvBRDF.y) * horiz * weight);

#elif defined(SUN_LIGHT)
	vec3 L2, H2;
	float NL2, EH2, NH2, L2H2;

	L2	= (u_PrimaryLightOrigin.xyz - position * u_PrimaryLightOrigin.w);
	H2  = normalize(L2 + E);
    NL2 = clamp(dot(N, L2), 0.0, 1.0);
    NL2 = max(1e-8, abs(NL2) );
    EH2 = max(1e-8, dot(E, H2));
    NH2 = max(1e-8, dot(N, H2));

	float shadowValue = texelFetch(u_ShadowMap, windowCoord, 0).r;

	attenuation  = NL2;
	attenuation *= shadowValue;

	vec3 reflectance = vec3(1.0);
	diffuseOut  = sqrt(u_PrimaryLightColor * reflectance * attenuation);
	
	reflectance  = CalcSpecular(specularAndGloss.rgb, NH2, NL2, NE, EH2, roughness);
	specularOut  = sqrt(u_PrimaryLightColor * reflectance * attenuation);

#elif defined(LIGHT_GRID)
  #if 1
	ivec3 gridSize = textureSize(u_LightGridDirectionalLightMap, 0);
	vec3 invGridSize = vec3(1.0) / vec3(gridSize);
	vec3 gridCell = (position - u_LightGridOrigin) * u_LightGridCellInverseSize * invGridSize;
	vec3 lightDirection = texture(u_LightGridDirectionMap, gridCell).rgb * 2.0 - vec3(1.0);
	vec3 directionalLight = texture(u_LightGridDirectionalLightMap, gridCell).rgb;
	vec3 ambientLight = texture(u_LightGridAmbientLightMap, gridCell).rgb;

	directionalLight *= directionalLight;
	ambientLight *= ambientLight;

	vec3 L = normalize(-lightDirection);
	float NdotL = clamp(dot(N, L), 0.0, 1.0);

	vec3 reflectance = 2.0 * u_LightGridDirectionalScale * (NdotL * directionalLight) +
		(u_LightGridAmbientScale * ambientLight);
	reflectance *= albedo;

	E = normalize(-var_ViewDir);
	H = normalize(L + E);
	EH = max(1e-8, dot(E, H));
	NH = max(1e-8, dot(N, H));
	NL = clamp(dot(N, L), 1e-8, 1.0);
	NE = abs(dot(N, E)) + 1e-5;

	reflectance += CalcSpecular(specularAndGloss.rgb, NH, NL, NE, EH, roughness);

	result = sqrt(reflectance);
  #else
	// Ray marching debug visualisation
	ivec3 gridSize = textureSize(u_LightGridDirectionalLightMap, 0);
	vec3 invGridSize = vec3(1.0) / vec3(gridSize);
	vec3 samplePosition = invGridSize * (u_ViewOrigin - u_LightGridOrigin) * u_LightGridCellInverseSize;
	vec3 stepSize = 0.5 * normalize(var_ViewDir) * invGridSize;
	float stepDistance = length(0.5 * u_LightGridCellInverseSize);
	float maxDistance = linearDepth;
	vec4 accum = vec4(0.0);
	float d = 0.0;

	for ( int i = 0; d < maxDistance && i < 50; i++ )
	{
		vec3 ambientLight = texture(u_LightGridAmbientLightMap, samplePosition).rgb;
		ambientLight *= 0.05;

		accum = (1.0 - accum.a) * vec4(ambientLight, 0.05) + accum;

		if ( accum.a > 0.98 )
		{
			break;
		}

		samplePosition += stepSize;
		d += stepDistance;

		if ( samplePosition.x < 0.0 || samplePosition.y < 0.0 || samplePosition.z < 0.0 ||
			samplePosition.x > 1.0 || samplePosition.y > 1.0 || samplePosition.z > 1.0 )
		{
			break;
		}
	}

	result = accum.rgb * 0.8;
  #endif
#endif
	
	out_Color = vec4(diffuseOut, 1.0);
	out_Glow  = vec4(specularOut, 1.0);
}