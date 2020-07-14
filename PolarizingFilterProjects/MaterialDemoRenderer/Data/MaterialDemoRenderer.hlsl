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
    CsmData gCsmData;
    float4x4 camVpAtLastCsmUpdate;
    float2 gRenderTargetDim;
    float gOpacityScale;
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




// TODO vectorize
float Psi_MetalApprox(float R0, float sinTheta, float cosTheta, float tanTheta)
{
    float d = sinTheta*tanTheta;

    float Ra = (1.095-R0);
    float Rb = (1.14-R0);

    float left  = (6.0*R0 + 0.1/(Ra*Ra*Ra));
    float right = (0.16*R0*R0)/(Rb*Rb) + 0.2*(cos(15.0*R0) + 1.0);

    return d/(left + d*d*right);
}


// TODO vectorize
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


// TODO Vectorize
// Filter that applies after Fresnel has already been multiplied
float LP_filterApprox(float R0, float sinTheta, float cosTheta, float tanTheta, float angle)
{
    float Psi_M = Psi_MetalApprox(R0, sinTheta, cosTheta, tanTheta);
    float Psi_D = Psi_DielectricExact(R0, sinTheta, tanTheta);

    //float s = smoothstep(0.18, 0.29, R0); // Smooth transition if specular values in that range are used
    float s = step(0.2, R0);
    float Psi = (1.0 - s)*Psi_D + s*Psi_M;

    return (Psi*cos(2.0*angle) + 1.0);
}

//TODO remove this function maybe?
// Get the normalized reference x vector from normalized y and z vectors
float3 computeX(float3 y, float3 z)
{
    return normalize(cross(y, z));
}

//TODO check if minus sign can be avoided
float calcRelAngle(float3 cameraX, float3 N, float3 V)
{
    float3 surfaceX = computeX(N, V);
    
    // Calculate rotation angle between surfaceX and cameraX
    float dotX = dot(surfaceX, cameraX);
    float detX = dot(-V, cross(surfaceX, cameraX)); // TODO double check these
    return atan2(detX, dotX); // angle between surfaceX and cameraX
}


float3 polarizingFilter(ShadingData sd, float3 L, float3 cameraX)
{
    //TODO move to constant buffer
    //float filterAngle = 3.14/2.0;
    float filterAngle = 0.0;
    float angle = filterAngle - calcRelAngle(cameraX, sd.N, sd.V);

    float NdotL = saturate(dot(sd.N, L));

    float sinTheta = length(cross(L, sd.N));
    float cosTheta = NdotL;
    float tanTheta = sinTheta/cosTheta;

    //Aproximate
    float RLP = LP_filterApprox(sd.specular.r, sinTheta, cosTheta, tanTheta, angle);
    float GLP = LP_filterApprox(sd.specular.g, sinTheta, cosTheta, tanTheta, angle);
    float BLP = LP_filterApprox(sd.specular.b, sinTheta, cosTheta, tanTheta, angle);

    //Correct
    //TODO
    
    return float3(RLP, GLP, BLP);
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
    float3 filter = polarizingFilter(sd, ls.L, cameraX);
    
    sr.color.rgb += (sr.specular*filter);
    //sr.color.rgb += (sr.specular);

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
    float3 filter = polarizingFilter(sd, ls.L, cameraX);

    sr.color.rgb += (sr.specular*filter);
    //sr.color.rgb += sr.specular;

    return sr;
}



PsOut ps(MainVsOut vOut, float4 pixelCrd : SV_POSITION)
{
    PsOut psOut;

    ShadingData sd = prepareShadingData(vOut.vsData, gMaterial, gCamera.posW);

    float4 finalColor = float4(0, 0, 0, 1);

    //TODO
    // TODO? use up-vector instead?
    // Calculate cameras X 
    // camera's x points up, y points left //TODO? check that this is correct

    //normalize(gCamera.cameraV); // <- camera up vector. See `HostDeviceSharedCode.h`
    //normalize(gCamera.cameraW); // <- camera forward vector. See `HostDeviceSharedCode.h`
    //float3 cameraUp = normalize(invView[1].xyz);
    //float3 cameraX  = computeX(cameraUp, -rayDirW); // This points to the right
    float3 cameraUp      = normalize(gCamera.cameraV);
    float3 cameraForward = normalize(gCamera.cameraW);

    //TODO these calculations are probably not needed since `cameraU` is the camera right vector
    //     If that is the case, then remove the cameraX param from the code (but include it in the article)
    float3 cameraX       = computeX(cameraUp, -cameraForward); // This points to the right //TODO double-check


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
