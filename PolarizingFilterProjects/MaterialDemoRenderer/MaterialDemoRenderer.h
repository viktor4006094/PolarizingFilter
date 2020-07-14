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
#pragma once
#include "Falcor.h"
#include "MaterialDemoRendererSceneRenderer.h"


//TODO move this to its own file

/**
Some X Macros to make it easy to add more metals

Arguments
    name: The name of the metal
    nr  : IoR n at 680 nm (red)
    ng  : IoR n at 530 nm (green)
    nb  : IoR n at 470 nm (blue)
    kr  : IoR k at 680 nm (red)
    kg  : IoR k at 530 nm (green)
    kb  : IoR k at 470 nm (blue)
*/
#define EXPAND( x ) x

#define X_NAME_(name, nr, ng, nb, kr, kg, kb, ...) name,
#define X_DROP_(name, nr, ng, nb, kr, kg, kb, ...) { MetalPreset::##name, #name },
#define X_IOR_N_(name, nr, ng, nb, kr, kg, kb, ...) { nr, ng, nb },
#define X_IOR_K_(name, nr, ng, nb, kr, kg, kb, ...) { kr, kg, kb },

#define X_NAME(...)  EXPAND( X_NAME_(__VA_ARGS__) )
#define X_DROP(...)  EXPAND( X_DROP_(__VA_ARGS__) )
#define X_IOR_N(...) EXPAND( X_IOR_N_(__VA_ARGS__) )
#define X_IOR_K(...) EXPAND( X_IOR_K_(__VA_ARGS__) )

// Materials from refractiveindex.info
#define METAL_TABLE \
X(Aluminum  , 1.346f, 0.965f, 0.617f, 7.475f, 6.400f, 5.303) \
X(Brass     , 0.444f, 0.527f, 1.094f, 3.695f, 2.765f, 1.829) \
X(Copper    , 0.271f, 0.677f, 1.316f, 3.609f, 2.625f, 2.292) \
X(Gold      , 0.183f, 0.421f, 1.373f, 3.424f, 2.346f, 1.770) \
X(Iron      , 2.911f, 2.950f, 2.585f, 3.089f, 2.932f, 2.767) \
X(Lead      , 1.910f, 1.830f, 1.440f, 3.510f, 3.400f, 3.180) \
X(Platinum  , 2.376f, 2.085f, 1.845f, 4.266f, 3.715f, 3.137) \
X(Silver    , 0.159f, 0.145f, 0.135f, 3.929f, 3.190f, 2.381) \
X(Titanium  , 2.741f, 2.542f, 2.267f, 3.814f, 3.435f, 3.039) \
X(Glass     , 1.521f, 1.525f, 1.532f, 0.000f, 0.000f, 0.000) \
X(Plastic   , 1.579f, 1.589f, 1.608f, 0.000f, 0.000f, 0.000)


//OLD VER
//X(Aluminium, 1.7680f  , 0.93029f , 0.70362f , 7.9819f, 6.3965f, 5.6953f) \
//X(Brass,     0.44400f , 0.57300f , 0.90000f , 3.9430f, 2.5680f, 1.9570f) \
//X(Copper,    0.22905f , 0.82310f , 1.2438f  , 3.9073f, 2.4763f, 2.2880f) \
//X(Gold,      0.13544f , 0.55758f , 1.3148f  , 3.8820f, 2.2039f, 1.8534f) \
//X(Iron,      2.8920f  , 2.8889f  , 2.6660f  , 3.1420f, 2.9164f, 2.8175f) \
//X(Platium,   2.4710f  , 2.0332f  , 1.8906f  , 4.4177f, 3.6000f, 3.2520f) \
//X(Silver,    0.045444f, 0.053285f, 0.049317f, 4.6447f, 3.4101f, 2.8545f)

using namespace Falcor;

class MaterialDemoRenderer : public Renderer
{
public:
    void onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext) override;
    void onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo) override;
    void onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height) override;
    bool onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent) override;
    void onGuiRender(SampleCallbacks* pSample, Gui* pGui) override;
    void onDroppedFile(SampleCallbacks* pSample, const std::string& filename) override;

private:
#define X(...) X_NAME(__VA_ARGS__)
    enum MetalPreset : uint32_t
    {
        METAL_TABLE
        NUM_METALS
    };
#undef X

#define X(...) X_DROP(__VA_ARGS__)
    Falcor::Gui::DropdownList mMetalPresets =
    {
        METAL_TABLE
    };
#undef X

#define X(...) X_IOR_N(__VA_ARGS__)
    glm::vec3 mMetalPresetsN[MetalPreset::NUM_METALS] = { METAL_TABLE };
#undef X

#define X(...) X_IOR_K(__VA_ARGS__)
    glm::vec3 mMetalPresetsK[MetalPreset::NUM_METALS] = { METAL_TABLE };
#undef X

    Fbo::SharedPtr mpMainFbo;
    Fbo::SharedPtr mpDepthPassFbo;
    Fbo::SharedPtr mpResolveFbo;
    Fbo::SharedPtr mpPostProcessFbo;

    struct ShadowPass
    {
        bool updateShadowMap = true;
        CascadedShadowMaps::SharedPtr pCsm;
        Texture::SharedPtr pVisibilityBuffer;
        glm::mat4 camVpAtLastCsmUpdate = glm::mat4();
    };
    ShadowPass mShadowPass;

    //  SkyBox Pass.
    struct
    {
        SkyBox::SharedPtr pEffect;
        DepthStencilState::SharedPtr pDS;
        Sampler::SharedPtr pSampler;
    } mSkyBox;

    //  Lighting Pass.
    struct
    {
        GraphicsVars::SharedPtr pVars;
        GraphicsProgram::SharedPtr pProgram;
        DepthStencilState::SharedPtr pDsState;
        RasterizerState::SharedPtr pNoCullRS;
        BlendState::SharedPtr pAlphaBlendBS;
    } mLightingPass;

    struct
    {
        GraphicsVars::SharedPtr pVars;
        GraphicsProgram::SharedPtr pProgram;
    } mDepthPass;


    //  The Temporal Anti-Aliasing Pass.
    class
    {
    public:
        TemporalAA::SharedPtr pTAA;
        Fbo::SharedPtr getActiveFbo() { return pTAAFbos[activeFboIndex]; }
        Fbo::SharedPtr getInactiveFbo()  { return pTAAFbos[1 - activeFboIndex]; }
        void createFbos(uint32_t width, uint32_t height, const Fbo::Desc & fboDesc)
        {
            pTAAFbos[0] = FboHelper::create2D(width, height, fboDesc);
            pTAAFbos[1] = FboHelper::create2D(width, height, fboDesc);
        }

        void switchFbos() { activeFboIndex = 1 - activeFboIndex; }
        void resetFbos()
        {
            activeFboIndex = 0;
            pTAAFbos[0] = nullptr;
            pTAAFbos[1] = nullptr;
        }

        void resetFboActiveIndex() { activeFboIndex = 0;}

    private:
        Fbo::SharedPtr pTAAFbos[2];
        uint32_t activeFboIndex = 0;
    } mTAA;


    ToneMapping::SharedPtr mpToneMapper;

    struct
    {
        SSAO::SharedPtr pSSAO;
        FullScreenPass::UniquePtr pApplySSAOPass;
        GraphicsVars::SharedPtr pVars;
    } mSSAO;

    FXAA::SharedPtr mpFXAA;

    void beginFrame(RenderContext* pContext, Fbo* pTargetFbo, uint64_t frameId);
    void endFrame(RenderContext* pContext);
    void depthPass(RenderContext* pContext);
    void shadowPass(RenderContext* pContext);
    void renderSkyBox(RenderContext* pContext);
    void lightingPass(RenderContext* pContext, Fbo* pTargetFbo);
    //Need to resolve depth first to pass resolved depth to shadow pass
    void resolveDepthMSAA(RenderContext* pContext);
    void resolveMSAA(RenderContext* pContext);
    void executeFXAA(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);
    void runTAA(RenderContext* pContext, Fbo::SharedPtr pColorFbo);
    void postProcess(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);
    void ambientOcclusion(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);


    void renderOpaqueObjects(RenderContext* pContext);
    void renderTransparentObjects(RenderContext* pContext);

    void initSkyBox(const std::string& name);
    void initPostProcess();
    void initLightingPass();
    void initDepthPass();
    void initShadowPass(uint32_t windowWidth, uint32_t windowHeight);
    void initSSAO();
    void updateLightProbe(const LightProbe::SharedPtr& pLight);
    void initAA(SampleCallbacks* pSample);

    void initControls();

    GraphicsState::SharedPtr mpState;
	MaterialDemoRendererSceneRenderer::SharedPtr mpSceneRenderer;
    void loadModel(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar);
    void loadScene(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar);
    void initScene(SampleCallbacks* pSample, Scene::SharedPtr pScene);
    void applyCustomSceneVars(const Scene* pScene, const std::string& filename);
    void resetScene();

    void setActiveCameraAspectRatio(uint32_t w, uint32_t h);
    void setSceneSampler(uint32_t maxAniso);

    Sampler::SharedPtr mpSceneSampler;

    struct ProgramControl
    {
        bool enabled;
        bool unsetOnEnabled;
        std::string define;
        std::string value;
    };

    enum ControlID
    {
        SuperSampling,
        EnableShadows,
        EnableReflections,
        EnableSSAO,
        EnableHashedAlpha,
        EnableTransparency,
        VisualizeCascades,
        Count
    };

    enum class SamplePattern : uint32_t
    {
        Halton,
        DX11
    };

    enum class AAMode
    {
        None,
        MSAA,
        TAA,
        FXAA
    };

    float mOpacityScale = 0.5f;
    AAMode mAAMode = AAMode::TAA;
    uint32_t mMSAASampleCount = 4;
    SamplePattern mTAASamplePattern = SamplePattern::Halton;
    void applyAaMode(SampleCallbacks* pSample);
    std::vector<ProgramControl> mControls;
    void applyLightingProgramControl(ControlID controlID);

    // Polarizing Filter
    bool mEnablePolarizingFilter = true;
    float mPolarizingFilterAngle = 0.0f;

    // Material
    bool      mUseMaterial       = true;
    bool      mUseAsDielectric   = false;
    float     mMaterialRoughness = 0.2f;

    uint32    mSelectedMetal = MetalPreset::Gold;
    glm::vec3 mMetalIoRn     = mMetalPresetsN[MetalPreset::Gold];
    glm::vec3 mMetalIoRk     = mMetalPresetsK[MetalPreset::Gold];



    bool mUseCameraPath = true;
    void applyCameraPathState();
    bool mPerMaterialShader = false;
    bool mEnableDepthPass = true;
    bool mUseCsSkinning = false;
    void applyCsSkinningMode();
    static const std::string skDefaultScene;

    void createTaaPatternGenerator(uint32_t fboWidth, uint32_t fboHeight);
};
