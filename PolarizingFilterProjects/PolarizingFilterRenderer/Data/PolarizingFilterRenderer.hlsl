/***************************************************************************
# Copyright (c) 2015, NVIDIA CORPORATION.
# Copyright (c) 2020, Viktor Enfeldt.
# All rights reserved.
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
__import Lights;

layout(binding = 0) cbuffer PerFrameCB : register(b0)
{
    CsmData gCsmData;
    float4x4 camVpAtLastCsmUpdate;
    float2 gRenderTargetDim;
    float gOpacityScale;

    float gPolarizingFilterAngle;
    bool  gEnablePolarizingFilter;
};

layout(set = 1, binding = 1) SamplerState gSampler;
Texture2D gVisibilityBuffer;


// Calculate rotation angle between the camera and the surface
float calcRelativeAngle(float3 cameraX, float3 N, float3 V)
{
    float3 surfaceX = normalize(cross(N, V));
    float dotX = dot(surfaceX, cameraX);
    float detX = dot(V, cross(surfaceX, cameraX)); // TODO double check these
    return gPolarizingFilterAngle + atan2(detX, dotX);
}

// R0  - specular color
// ct  - cos(theta)
// st2 - sin^2(theta)
float3 psi_MetalApprox(float3 R0, float ct, float st2)
{
    float3 Rb = float3(1.095) - R0;
    float3 Rg = float3(1.18) - R0;
    float3 beta  = float3(0.1)/(Rb*Rb*Rb) + 5.4*R0*R0 + float3(1.0);
    float3 gamma = (0.16*R0*R0)/(Rg*Rg) + 0.35*Rg;
    
    float3 psi = (ct*st2)/(beta*ct*ct + gamma*st2*st2);
    return saturate(psi);
}

// R0  - specular color
// ct  - cos(theta)
// st2 - sin^2(theta)
float psi_DielectricExact(float R0, float ct, float st2)
{
    float n = (1.0 + sqrt(R0))/(1.0 - sqrt(R0));
    float c = n*n - st2;

    float psi = (2.0*sqrt(c)*ct*st2)/(c*ct*ct + st2*st2);

    return saturate(psi);
}

float3 polarizingFilter(ShadingData sd, LightSample ls, float3 cameraX)
{
    float3 H = normalize(sd.V + ls.L);
    float st  = length(cross(ls.L, H)); // sin(theta)
    float st2 = st*st;                  // sin^2(theta)
    float angle = calcRelativeAngle(cameraX, H, sd.V);

    float3 psi;
    if (sd.metalness > 0.5) {
        psi = psi_MetalApprox(sd.specular, ls.LdotH, st2);
    } else {
        psi = float3(psi_DielectricExact(sd.specular.r, ls.LdotH, st2));
    }

    return (cos(2.0*angle)*psi + float3(1.0));
}


//// Point and directional light sources ////
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
        float3 filter = polarizingFilter(sd, ls, cameraX);
        sr.specular *= filter;
    }

    sr.color.rgb += sr.specular;

    // Apply the shadow factor
    sr.color.rgb *= shadowFactor;

    return sr;
}

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
        float3 filter = polarizingFilter(sd, ls, cameraX);
        sr.specular *= filter;
    }
    
    sr.color.rgb += sr.specular;

    return sr;
}





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

PsOut ps(MainVsOut vOut, float4 pixelCrd : SV_POSITION)
{
    PsOut psOut;

    ShadingData sd = prepareShadingData(vOut.vsData, gMaterial, gCamera.posW);

    float4 finalColor = float4(0, 0, 0, 1);

    float3 cameraUp = normalize(gCamera.cameraV);
    float3 cameraX  = normalize(cross(cameraUp, sd.V)); // This points to the right //TODO double-check

    [unroll]
    for (uint l = 0; l < _LIGHT_COUNT; l++)
    {
        float shadowFactor = 1;
#ifdef _ENABLE_SHADOWS
        if (l == 0)
        {
            shadowFactor = gVisibilityBuffer.Load(int3(vOut.vsData.posH.xy, 0)).r;
            shadowFactor *= sd.opacity;
        }
#endif
        finalColor.rgb += evalMaterialWithFilter(sd, gLights[l], shadowFactor, cameraX).color.rgb;
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
