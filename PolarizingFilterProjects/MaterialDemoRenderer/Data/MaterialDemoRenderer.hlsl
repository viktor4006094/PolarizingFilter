/***************************************************************************
# Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/
__import ShaderCommon;
__import DefaultVS;
__import Effects.CascadedShadowMap;
__import Shading;
__import Helpers;
__import BRDF;


// Needed for evalMaterialWithFilter
__import Lights;


layout(binding = 0) cbuffer PerFrameCB : register(b0)
{
    CsmData  gCsmData;
    float4x4 camVpAtLastCsmUpdate;
    float2   gRenderTargetDim;
    float    gOpacityScale;

    float gPolarizingFilterAngle;
    bool  gEnablePolarizingFilter;
    bool  gUseExactPsi;
    
    bool   gCustomMaterial;
    float3 gIOR_n;
    float3 gIOR_k;
    float  gRoughness;
    bool   gUseAsDielectric;
};

layout(set = 1, binding = 1) SamplerState gSampler;
Texture2D gVisibilityBuffer;

struct MainVsOut
{
    VertexOut vsData;
    float shadowsDepthC : DEPTH;
};

MainVsOut vs(VertexIn vIn)
{
    MainVsOut vsOut;
    vsOut.vsData = defaultVS(vIn);

#ifdef _OUTPUT_MOTION_VECTORS
    vsOut.vsData.prevPosH.xy += vsOut.vsData.prevPosH.w * 2 * float2(gCamera.jitterX, gCamera.jitterY);
#endif

    vsOut.shadowsDepthC = mul(float4(vsOut.vsData.posW, 1), camVpAtLastCsmUpdate).z;
    return vsOut;
}

struct PsOut
{
    float4 color : SV_TARGET0;
    float4 normal : SV_TARGET1;
#ifdef _OUTPUT_MOTION_VECTORS
    float2 motion : SV_TARGET2;
#endif
};






// TODO Move all these functions to their own file


//TODO get rid of tan, move out all common stuff

float3 SpecularFromIOR(float3 n, float3 k) {
    float3 A = (n - float3(1.0));
    float3 B = (n + float3(1.0));

    return saturate((A*A + k*k)/(B*B + k*k));
}

float3 Psi_Exact(float3 n, float3 k, float sinTheta, float tanTheta)
{
	float3 n2 = n*n;
	float3 k2 = k*k;

	float3 f = n2 - k2 - float3(sinTheta*sinTheta);
	float3 e = sqrt(f*f + 4.0*n2*k2);
	float  d = sinTheta*tanTheta;
	float3 c = sqrt(e + f);

    return (sqrt(2.0)*d*c)/(e + float3(d*d));
}


float3 Psi_MetalApprox(float3 R0, float sinTheta, float cosTheta, float tanTheta)
{
    float d = sinTheta*tanTheta;

    float3 Rb = (float3(1.095)-R0);
    float3 Rg = (float3(1.14)-R0);

    float3 beta  = 0.1/(Rb*Rb*Rb) + 5.4*R0*R0 + 1.0;
    float3 gamma = (0.16*R0*R0)/(Rg*Rg) + 0.15*cos(15.0*R0) + 0.15;

    return saturate(d/(beta + gamma*d*d));
}


float Psi_DielectricExact(float R0, float sinTheta, float tanTheta)
{
    // For dielectrics R0 is always small, but R0 values for metals are used by this function to so
    // a min() is required to prevent division by zero.
    float n   = (1.0 + sqrt(R0))/(1.0 - min(sqrt(R0), 0.5));
    float n2  = n*n;
    float d   = sinTheta*tanTheta;
    float st2 = sinTheta*sinTheta;

    return (2.0*sqrt(n2 - st2)*d)/(n2 - st2 + d*d);
}


// Assuming that all dilectrics have R0 = 0.04 i.e., n = 1.5
float Psi_DielectricOneFive(float sinTheta, float tanTheta)
{
    float d   = sinTheta*tanTheta;
    float st2 = sinTheta*sinTheta;

    return saturate((2.0*sqrt(2.25 - st2)*d)/(2.25 - st2 + d*d));
}

//TODO remove this function maybe?
// Get the normalized reference x vector from normalized y and z vectors
float3 computeX(float3 y, float3 z)
{
    return normalize(cross(y, z));
}

//TODO check if minus sign can be avoided
// Calculate rotation angle between the camera and the surface
float calcRelAngle(float3 cameraX, float3 N, float3 V)
{
    float3 surfaceX = computeX(N, V);
    float dotX = dot(surfaceX, cameraX);
    float detX = dot(-V, cross(surfaceX, cameraX)); // TODO double check these
    return atan2(detX, dotX); // angle between surfaceX and cameraX
}


float3 polarizingFilter(ShadingData sd, float3 L, float3 cameraX)
{
    float3 H = normalize(sd.V + L);

    float angle = -gPolarizingFilterAngle - calcRelAngle(cameraX, H, sd.V);

    float sinTheta = length(cross(L, H));
    float cosTheta = saturate(dot(H, L)); //TODO replace with ls.LdotH
    float tanTheta = sinTheta/cosTheta;

    // Cheaper
    float3 Psi;
    if (gUseExactPsi) {
        Psi = Psi_Exact(gIOR_n, gIOR_k, sinTheta, tanTheta);
    } else if (sd.isMetal) {
        Psi = Psi_MetalApprox(sd.specular, sinTheta, cosTheta, tanTheta);
        //Psi = 0;
    } else {
        //Psi = 0.0;
        //Psi = Psi_DielectricOneFive(sinTheta, tanTheta);
        Psi = Psi_DielectricExact(sd.specular.r, sinTheta, tanTheta);
    }

    return (Psi*cos(2.0*angle) + 1.0);
}


//// Normal light sources ////
ShadingResult evalMaterialWithFilter(ShadingData sd, LightData light, float shadowFactor, float3 cameraX)
{
    ShadingResult sr = initShadingResult();
    LightSample ls = evalLight(light, sd);

    // If the light doesn't hit the surface or we are viewing the surface from the back, return
    if(ls.NdotL <= 0) return sr;
    sd.NdotV = saturate(sd.NdotV);

    // Calculate the diffuse term
    sr.diffuseBrdf = saturate(evalDiffuseBrdf(sd, ls));
    sr.diffuse = ls.diffuse * sr.diffuseBrdf * ls.NdotL;
    sr.color.rgb = sr.diffuse;
    sr.color.a = sd.opacity;

    // Calculate the specular term
    sr.specularBrdf = saturate(evalSpecularBrdf(sd, ls));
    sr.specular = ls.specular * sr.specularBrdf * ls.NdotL;

    // Apply polarizing filter
    if (gEnablePolarizingFilter) {
        float3 filter = polarizingFilter(sd, normalize(ls.L), cameraX);
        sr.specular *= filter;
    }

    sr.color.rgb += sr.specular;

    // Apply the shadow factor
    sr.color.rgb *= shadowFactor;

    return sr;
};

//// Light probes ////
ShadingResult evalMaterialWithFilter(ShadingData sd, LightProbeData probe, float3 cameraX)
{
    ShadingResult sr = initShadingResult();
    LightSample ls = evalLightProbe(probe, sd);

    sr.diffuse = ls.diffuse;
    sr.color.rgb = sr.diffuse;
    sr.specular = ls.specular;

    // Apply polarizing filter
    if (gEnablePolarizingFilter) {
        float3 filter = polarizingFilter(sd, ls.L, cameraX);
        sr.specular *= filter;
    }
    
    sr.color.rgb += sr.specular;

    return sr;
}



PsOut ps(MainVsOut vOut, float4 pixelCrd : SV_POSITION)
{
    PsOut psOut;

    ShadingData sd = prepareShadingData(vOut.vsData, gMaterial, gCamera.posW);

    if (gCustomMaterial) {
        sd.diffuse = float3(0.0);
        sd.specular = SpecularFromIOR(gIOR_n, gIOR_k);
        sd.isMetal = !gUseAsDielectric;
        sd.linearRoughness = max(0.08, gRoughness); // Clamp the roughness so that the BRDF won't explode
        sd.roughness = sd.linearRoughness * sd.linearRoughness;
    }
    
    float4 finalColor = float4(0, 0, 0, 1);

    float3 cameraUp = normalize(gCamera.cameraV);
    float3 cameraX  = computeX(cameraUp, sd.V); // This points to the right //TODO double-check

    [unroll]
    for (uint l = 0; l < _LIGHT_COUNT; l++) {
        float shadowFactor = 1;
#ifdef _ENABLE_SHADOWS
        if (l == 0) {
            shadowFactor = gVisibilityBuffer.Load(int3(vOut.vsData.posH.xy, 0)).r;
            shadowFactor *= sd.opacity;
        }
#endif
        finalColor.rgb += evalMaterialWithFilter(sd, gLights[l], shadowFactor, cameraX).color.rgb;

        //TODO break loop for performance testing with fewer lights
    }

    // Add the emissive component
    finalColor.rgb += sd.emissive;

#ifdef _ENABLE_TRANSPARENCY
    finalColor.a = sd.opacity * gOpacityScale;
#endif

#ifdef _ENABLE_REFLECTIONS
    finalColor.rgb += evalMaterialWithFilter(sd, gLightProbe, cameraX).color.rgb;
#endif

    // Add light-map
    finalColor.rgb += sd.diffuse * sd.lightMap.rgb;

    psOut.color = finalColor;
    psOut.normal = float4(vOut.vsData.normalW * 0.5f + 0.5f, 1.0f);

#ifdef _OUTPUT_MOTION_VECTORS
    psOut.motion = calcMotionVector(pixelCrd.xy, vOut.vsData.prevPosH, gRenderTargetDim);
#endif

#if defined(_VISUALIZE_CASCADES) && defined(_ENABLE_SHADOWS)
    float3 cascadeColor = gVisibilityBuffer.Load(int3(vOut.vsData.posH.xy, 0)).gba;
    psOut.color.rgb *= cascadeColor;
#endif
    return psOut;
}
