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
#include "MaterialDemoRenderer.h"

// pi/180
#define DEG_TO_RAD 0.01745329238474369049072265625f

const std::string MaterialDemoRenderer::skDefaultScene = "../DemoScenes/MaterialTest.fscene";

void MaterialDemoRenderer::initDepthPass()
{
    mDepthPass.pProgram = GraphicsProgram::createFromFile("DepthPass.ps.slang", "", "main");
    mDepthPass.pVars = GraphicsVars::create(mDepthPass.pProgram->getReflector());
}

void MaterialDemoRenderer::initLightingPass()
{
    mLightingPass.pProgram = GraphicsProgram::createFromFile("MaterialDemoRenderer.hlsl", "vs", "ps");
    mLightingPass.pProgram->addDefine("_LIGHT_COUNT", std::to_string(mpSceneRenderer->getScene()->getLightCount()));
    initControls();
    mLightingPass.pVars = GraphicsVars::create(mLightingPass.pProgram->getReflector());
    
    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthTest(true).setStencilTest(false)./*setDepthWriteMask(false).*/setDepthFunc(DepthStencilState::Func::LessEqual);
    mLightingPass.pDsState = DepthStencilState::create(dsDesc);

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::None);
    mLightingPass.pNoCullRS = RasterizerState::create(rsDesc);

    BlendState::Desc bsDesc;
    bsDesc.setRtBlend(0, true).setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::One, BlendState::BlendFunc::Zero);
    mLightingPass.pAlphaBlendBS = BlendState::create(bsDesc);
}

void MaterialDemoRenderer::initShadowPass(uint32_t windowWidth, uint32_t windowHeight)
{
    mShadowPass.pCsm = CascadedShadowMaps::create(mpSceneRenderer->getScene()->getLight(0), 2048, 2048, windowWidth, windowHeight, mpSceneRenderer->getScene()->shared_from_this());
    mShadowPass.pCsm->setFilterMode(CsmFilterEvsm4);
    mShadowPass.pCsm->setVsmLightBleedReduction(0.3f);
    mShadowPass.pCsm->setVsmMaxAnisotropy(4);
    mShadowPass.pCsm->setEvsmBlur(7, 3);
}

void MaterialDemoRenderer::initSSAO()
{
    mSSAO.pSSAO = SSAO::create(uvec2(1024));
    mSSAO.pApplySSAOPass = FullScreenPass::create("ApplyAO.ps.slang");
    mSSAO.pVars = GraphicsVars::create(mSSAO.pApplySSAOPass->getProgram()->getReflector());

    Sampler::Desc desc;
    desc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mSSAO.pVars->setSampler("gSampler", Sampler::create(desc));
}

void MaterialDemoRenderer::setSceneSampler(uint32_t maxAniso)
{
    Scene* pScene = mpSceneRenderer->getScene().get();
    Sampler::Desc samplerDesc;
    samplerDesc.setAddressingMode(Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(maxAniso);
    mpSceneSampler = Sampler::create(samplerDesc);
    pScene->bindSampler(mpSceneSampler);
}

void MaterialDemoRenderer::applyCustomSceneVars(const Scene* pScene, const std::string& filename)
{
    std::string folder = getDirectoryFromFile(filename);

    Scene::UserVariable var = pScene->getUserVariable("sky_box");
    if (var.type == Scene::UserVariable::Type::String) initSkyBox(folder + '/' + var.str);

    var = pScene->getUserVariable("opacity_scale");
    if (var.type == Scene::UserVariable::Type::Double) mOpacityScale = (float)var.d64;
}

void MaterialDemoRenderer::initScene(SampleCallbacks* pSample, Scene::SharedPtr pScene)
{
    if (pScene->getCameraCount() == 0)
    {
        // Place the camera above the center, looking slightly downwards
        const Model* pModel = pScene->getModel(0).get();
        Camera::SharedPtr pCamera = Camera::create();

        vec3 position = pModel->getCenter();
        float radius = pModel->getRadius();
        position.y += 0.1f * radius;
        pScene->setCameraSpeed(radius * 0.03f);

        pCamera->setPosition(position);
        pCamera->setTarget(position + vec3(0, -0.3f, -radius));
        pCamera->setDepthRange(0.1f, radius * 10);

        pScene->addCamera(pCamera);
    }

    if (pScene->getLightCount() == 0)
    {
        // Create a directional light
        DirectionalLight::SharedPtr pDirLight = DirectionalLight::create();
        pDirLight->setWorldDirection(vec3(-0.189f, -0.861f, -0.471f));
        pDirLight->setIntensity(vec3(0.f, 0.f, 0.f));
        pDirLight->setName("DirLight");
        pScene->addLight(pDirLight);
    }

    if (pScene->getLightProbeCount() > 0)
    {
        const LightProbe::SharedPtr& pProbe = pScene->getLightProbe(0);
        pProbe->setRadius(pScene->getRadius());
        pProbe->setPosW(pScene->getCenter());
        pProbe->setSampler(mpSceneSampler);
    }

    mpSceneRenderer = MaterialDemoRendererSceneRenderer::create(pScene);
    mpSceneRenderer->setCameraControllerType(SceneRenderer::CameraControllerType::FirstPerson);
    mpSceneRenderer->toggleStaticMaterialCompilation(mPerMaterialShader);
    setSceneSampler(mpSceneSampler ? mpSceneSampler->getMaxAnisotropy() : 16);
    setActiveCameraAspectRatio(pSample->getCurrentFbo()->getWidth(), pSample->getCurrentFbo()->getHeight());
    initDepthPass();
    initLightingPass();
    auto pTargetFbo = pSample->getCurrentFbo();
    initShadowPass(pTargetFbo->getWidth(), pTargetFbo->getHeight());
    initSSAO();
    initAA(pSample);

    mControls[EnableReflections].enabled = pScene->getLightProbeCount() > 0;
    applyLightingProgramControl(ControlID::EnableReflections);
    
    pSample->setCurrentTime(0);
}

void MaterialDemoRenderer::resetScene()
{
    mpSceneRenderer = nullptr;
    mSkyBox.pEffect = nullptr;
}

void MaterialDemoRenderer::loadModel(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar)
{
    Mesh::resetGlobalIdCounter();
    resetScene();

    ProgressBar::SharedPtr pBar;
    if (showProgressBar)
    {
        pBar = ProgressBar::create("Loading Model");
    }

    Model::SharedPtr pModel = Model::createFromFile(filename.c_str());
    if (!pModel) return;
    Scene::SharedPtr pScene = Scene::create();
    pScene->addModelInstance(pModel, "instance");

    initScene(pSample, pScene);
}

void MaterialDemoRenderer::loadScene(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar)
{
    Mesh::resetGlobalIdCounter();
    resetScene();

    ProgressBar::SharedPtr pBar;
    if (showProgressBar)
    {
        pBar = ProgressBar::create("Loading Scene", 100);
    }

    Scene::SharedPtr pScene = Scene::loadFromFile(filename);

    if (pScene != nullptr)
    {
        initScene(pSample, pScene);
        applyCustomSceneVars(pScene.get(), filename);
        applyCsSkinningMode();
    }
}

void MaterialDemoRenderer::initSkyBox(const std::string& name)
{
    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mSkyBox.pSampler = Sampler::create(samplerDesc);
    mSkyBox.pEffect = SkyBox::create(name, true, mSkyBox.pSampler);
    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthFunc(DepthStencilState::Func::Always);
    mSkyBox.pDS = DepthStencilState::create(dsDesc);
}

void MaterialDemoRenderer::updateLightProbe(const LightProbe::SharedPtr& pLight)
{
    Scene::SharedPtr pScene = mpSceneRenderer->getScene();

    // Remove existing light probes
    while (pScene->getLightProbeCount() > 0)
    {
        pScene->deleteLightProbe(0);
    }

    pLight->setRadius(pScene->getRadius());
    pLight->setPosW(pScene->getCenter());
    pLight->setSampler(mpSceneSampler);
    pScene->addLightProbe(pLight);

    mControls[EnableReflections].enabled = true;
    applyLightingProgramControl(ControlID::EnableReflections);
}

void MaterialDemoRenderer::initAA(SampleCallbacks* pSample)
{
    mTAA.pTAA = TemporalAA::create();
    mpFXAA = FXAA::create();
    applyAaMode(pSample);
}

void MaterialDemoRenderer::initPostProcess()
{
    mpToneMapper = ToneMapping::create(ToneMapping::Operator::Clamp);
}

void MaterialDemoRenderer::onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext)
{
    mpState = GraphicsState::create();    
    initPostProcess();
    loadScene(pSample, skDefaultScene, true);
}

void MaterialDemoRenderer::renderSkyBox(RenderContext* pContext)
{
    if (mSkyBox.pEffect)
    {
        PROFILE("skyBox");
        mpState->setDepthStencilState(mSkyBox.pDS);
        mSkyBox.pEffect->render(pContext, mpSceneRenderer->getScene()->getActiveCamera().get());
        mpState->setDepthStencilState(nullptr);
    }
}

void MaterialDemoRenderer::beginFrame(RenderContext* pContext, Fbo* pTargetFbo, uint64_t frameId)
{
    pContext->pushGraphicsState(mpState);
    pContext->clearFbo(mpMainFbo.get(), glm::vec4(0.7f, 0.7f, 0.7f, 1.0f), 1, 0, FboAttachmentType::All);
    pContext->clearFbo(mpPostProcessFbo.get(), glm::vec4(), 1, 0, FboAttachmentType::Color);

    if (mAAMode == AAMode::TAA)
    {
        glm::vec2 targetResolution = glm::vec2(pTargetFbo->getWidth(), pTargetFbo->getHeight());
        pContext->clearRtv(mpMainFbo->getColorTexture(2)->getRTV().get(), vec4(0));

        //  Select the sample pattern and set the camera jitter
    }
}

void MaterialDemoRenderer::endFrame(RenderContext* pContext)
{
    pContext->popGraphicsState();
}

void MaterialDemoRenderer::postProcess(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("postProcess");    
    mpToneMapper->execute(pContext, mpResolveFbo->getColorTexture(0), pTargetFbo);
}

void MaterialDemoRenderer::depthPass(RenderContext* pContext)
{
    PROFILE("depthPass");
    if (mEnableDepthPass == false) 
    {
        return;
    }

    mpState->setFbo(mpDepthPassFbo);
    mpState->setProgram(mDepthPass.pProgram);
    pContext->setGraphicsVars(mDepthPass.pVars);
    
    auto renderMode = mControls[EnableTransparency].enabled ? MaterialDemoRendererSceneRenderer::Mode::Opaque : MaterialDemoRendererSceneRenderer::Mode::All;
    mpSceneRenderer->setRenderMode(renderMode);
    mpSceneRenderer->renderScene(pContext);
}

void MaterialDemoRenderer::lightingPass(RenderContext* pContext, Fbo* pTargetFbo)
{
    PROFILE("lightingPass");
    mpState->setProgram(mLightingPass.pProgram);
    mpState->setDepthStencilState(mEnableDepthPass ? mLightingPass.pDsState : nullptr);
    pContext->setGraphicsVars(mLightingPass.pVars);
    ConstantBuffer::SharedPtr pCB = mLightingPass.pVars->getConstantBuffer("PerFrameCB");
    pCB["gOpacityScale"] = mOpacityScale;

    // Polarizing filter
    pCB["gPolarizingFilterAngle"] = mPolarizingFilterAngle*DEG_TO_RAD;
    pCB["gEnablePolarizingFilter"] = mEnablePolarizingFilter;
    pCB["gUseExactPsi"] = mUseExactPsi;
    pCB["gShowDiff"] = mShowDiff;

    // Materials
    pCB["gIOR_n"] = mMaterialIoRn;
    pCB["gIOR_k"] = mMaterialIoRk;
    pCB["gRoughness"] = mMaterialRoughness;
    pCB["gUseAsDielectric"] = mUseAsDielectric;

    if (mControls[ControlID::EnableShadows].enabled)
    {
        pCB["camVpAtLastCsmUpdate"] = mShadowPass.camVpAtLastCsmUpdate;
        mLightingPass.pVars->setTexture("gVisibilityBuffer", mShadowPass.pVisibilityBuffer);
    }

    if (mAAMode == AAMode::TAA)
    {
        pContext->clearFbo(mTAA.getActiveFbo().get(), vec4(0.0, 0.0, 0.0, 0.0), 1, 0, FboAttachmentType::Color);
        pCB["gRenderTargetDim"] = glm::vec2(pTargetFbo->getWidth(), pTargetFbo->getHeight());
    }

    if(mControls[EnableTransparency].enabled)
    {
        renderOpaqueObjects(pContext);
        renderTransparentObjects(pContext);
    }
    else
    {
        mpSceneRenderer->setRenderMode(MaterialDemoRendererSceneRenderer::Mode::All);
        mpSceneRenderer->renderScene(pContext);
    }
    pContext->flush();
    mpState->setDepthStencilState(nullptr);
}

void MaterialDemoRenderer::renderOpaqueObjects(RenderContext* pContext)
{
    mpSceneRenderer->setRenderMode(MaterialDemoRendererSceneRenderer::Mode::Opaque);
    mpSceneRenderer->renderScene(pContext);
}

void MaterialDemoRenderer::renderTransparentObjects(RenderContext* pContext)
{
    mpSceneRenderer->setRenderMode(MaterialDemoRendererSceneRenderer::Mode::Transparent);
    mpState->setBlendState(mLightingPass.pAlphaBlendBS);
    mpState->setRasterizerState(mLightingPass.pNoCullRS);
    mpSceneRenderer->renderScene(pContext);
    mpState->setBlendState(nullptr);
    mpState->setRasterizerState(nullptr);
}

void MaterialDemoRenderer::resolveDepthMSAA(RenderContext* pContext)
{
    if (mAAMode == AAMode::MSAA)
    {
        pContext->resolveResource(mpMainFbo->getDepthStencilTexture(), mpResolveFbo->getColorTexture(2));
    }
}

void MaterialDemoRenderer::resolveMSAA(RenderContext* pContext)
{
    if(mAAMode == AAMode::MSAA)
    {
        PROFILE("resolveMSAA");
        pContext->resolveResource(mpMainFbo->getColorTexture(0), mpResolveFbo->getColorTexture(0));
        pContext->resolveResource(mpMainFbo->getColorTexture(1), mpResolveFbo->getColorTexture(1));
    }
}

void MaterialDemoRenderer::shadowPass(RenderContext* pContext)
{
    PROFILE("shadowPass");
    if (mControls[EnableShadows].enabled && mShadowPass.updateShadowMap)
    {
        mShadowPass.camVpAtLastCsmUpdate = mpSceneRenderer->getScene()->getActiveCamera()->getViewProjMatrix();
        Texture::SharedPtr pDepth;
        if (mAAMode == AAMode::MSAA)
        {
            pDepth = mpResolveFbo->getColorTexture(2);
        }
        else
        {
            pDepth = mpDepthPassFbo->getDepthStencilTexture();
        }
        mShadowPass.pVisibilityBuffer = mShadowPass.pCsm->generateVisibilityBuffer(pContext, mpSceneRenderer->getScene()->getActiveCamera().get(), mEnableDepthPass ? pDepth : nullptr);
        pContext->flush();
    }
}

void MaterialDemoRenderer::runTAA(RenderContext* pContext, Fbo::SharedPtr pColorFbo)
{
    if(mAAMode == AAMode::TAA)
    {
        PROFILE("runTAA");
        //  Get the Current Color and Motion Vectors
        const Texture::SharedPtr pCurColor = pColorFbo->getColorTexture(0);
        const Texture::SharedPtr pMotionVec = mpMainFbo->getColorTexture(2);

        //  Get the Previous Color
        const Texture::SharedPtr pPrevColor = mTAA.getInactiveFbo()->getColorTexture(0);

        //  Execute the Temporal Anti-Aliasing
        pContext->getGraphicsState()->pushFbo(mTAA.getActiveFbo());
        mTAA.pTAA->execute(pContext, pCurColor, pPrevColor, pMotionVec);
        pContext->getGraphicsState()->popFbo();

        //  Copy over the Anti-Aliased Color Texture
        pContext->blit(mTAA.getActiveFbo()->getColorTexture(0)->getSRV(0, 1), pColorFbo->getColorTexture(0)->getRTV());

        //  Swap the Fbos
        mTAA.switchFbos();
    }
}

void MaterialDemoRenderer::ambientOcclusion(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("ssao");
    if (mControls[EnableSSAO].enabled)
    {
        Texture::SharedPtr pDepth = (mAAMode == AAMode::MSAA) ? mpResolveFbo->getColorTexture(2) : mpResolveFbo->getDepthStencilTexture();
        Texture::SharedPtr pAOMap = mSSAO.pSSAO->generateAOMap(pContext, mpSceneRenderer->getScene()->getActiveCamera().get(), pDepth, mpResolveFbo->getColorTexture(1));
        mSSAO.pVars->setTexture("gColor", mpPostProcessFbo->getColorTexture(0));
        mSSAO.pVars->setTexture("gAOMap", pAOMap);

        pContext->getGraphicsState()->setFbo(pTargetFbo);
        pContext->setGraphicsVars(mSSAO.pVars);

        mSSAO.pApplySSAOPass->execute(pContext);
    }
}

void MaterialDemoRenderer::executeFXAA(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("fxaa");
    if(mAAMode == AAMode::FXAA)
    {
        pContext->blit(pTargetFbo->getColorTexture(0)->getSRV(), mpResolveFbo->getRenderTargetView(0));
        mpFXAA->execute(pContext, mpResolveFbo->getColorTexture(0), pTargetFbo);
    }
}

void MaterialDemoRenderer::onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    if (mpSceneRenderer)
    {
        beginFrame(pRenderContext, pTargetFbo.get(), pSample->getFrameID());
        {
            PROFILE("updateScene");
            mpSceneRenderer->update(pSample->getCurrentTime());
        }

        depthPass(pRenderContext);
        resolveDepthMSAA(pRenderContext); // Only runs in MSAA mode
        shadowPass(pRenderContext);
        mpState->setFbo(mpMainFbo);
        renderSkyBox(pRenderContext);
        lightingPass(pRenderContext, pTargetFbo.get());
        resolveMSAA(pRenderContext);      // This will only run if we are in MSAA mode

        Fbo::SharedPtr pPostProcessDst = mControls[EnableSSAO].enabled ? mpPostProcessFbo : pTargetFbo;
        postProcess(pRenderContext, pPostProcessDst);
        runTAA(pRenderContext, pPostProcessDst); // This will only run if we are in TAA mode
        ambientOcclusion(pRenderContext, pTargetFbo);
        executeFXAA(pRenderContext, pTargetFbo);

        endFrame(pRenderContext);
    }
    else
    {
        pRenderContext->clearFbo(pTargetFbo.get(), vec4(0.2f, 0.4f, 0.5f, 1), 1, 0);
    }

}

void MaterialDemoRenderer::applyCameraPathState()
{
    const Scene* pScene = mpSceneRenderer->getScene().get();
    if(pScene->getPathCount())
    {
        mUseCameraPath = mUseCameraPath;
        if (mUseCameraPath)
        {
            pScene->getPath(0)->attachObject(pScene->getActiveCamera());
        }
        else
        {
            pScene->getPath(0)->detachObject(pScene->getActiveCamera());
        }
    }
}

bool MaterialDemoRenderer::onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent)
{
    if (mpSceneRenderer && keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        std::string fileName = mMaterialPresets[mSelectedMetal].label + (mUseExactPsi ? "Exact" : "Approx");
        //std::string fileName = mMetalPresets[mSelectedMetal].label + "Unfiltered";
        //std::string fileName = mMetalPresets[mSelectedMetal].label + "Diff";

        switch (keyEvent.key)
        {
        case KeyboardEvent::Key::Minus:
            mUseCameraPath = !mUseCameraPath;
            applyCameraPathState();
            return true;
        case KeyboardEvent::Key::Key1:
            pSample->captureScreen(fileName.c_str(), "MaterialScreenshots");
            return true;
        case KeyboardEvent::Key::O:
            mPerMaterialShader = !mPerMaterialShader;
            mpSceneRenderer->toggleStaticMaterialCompilation(mPerMaterialShader);
            return true;
        }
    }

    return mpSceneRenderer ? mpSceneRenderer->onKeyEvent(keyEvent) : false;
}

void MaterialDemoRenderer::onDroppedFile(SampleCallbacks* pSample, const std::string& filename)
{
    if (hasSuffix(filename, ".fscene", false) == false)
    {
        msgBox("You can only drop a scene file into the window");
        return;
    }
    loadScene(pSample, filename, true);
}

bool MaterialDemoRenderer::onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent)
{
    return mpSceneRenderer ? mpSceneRenderer->onMouseEvent(mouseEvent) : true;
}

void MaterialDemoRenderer::onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height)
{
    // Create the post-process FBO and AA resolve Fbo
    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(0, ResourceFormat::RGBA8UnormSrgb);
    mpPostProcessFbo = FboHelper::create2D(width, height, fboDesc);

    applyAaMode(pSample);
    mShadowPass.pCsm->onResize(width, height);

    if(mpSceneRenderer)
    {
        setActiveCameraAspectRatio(width, height);
    }
}

void MaterialDemoRenderer::applyCsSkinningMode()
{
    if(mpSceneRenderer)
    {
        SkinningCache::SharedPtr pCache = mUseCsSkinning ? SkinningCache::create() : nullptr;
        mpSceneRenderer->getScene()->attachSkinningCacheToModels(pCache);
    }    
}

void MaterialDemoRenderer::setActiveCameraAspectRatio(uint32_t w, uint32_t h)
{
    mpSceneRenderer->getScene()->getActiveCamera()->setAspectRatio((float)w / (float)h);
}

#ifdef _WIN32
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
#else
int main(int argc, char** argv)
#endif
{
    MaterialDemoRenderer::UniquePtr pRenderer = std::make_unique<MaterialDemoRenderer>();
    SampleConfig config;
    config.windowDesc.title = "Material Demo Renderer";
    config.windowDesc.resizableWindow = false;

    config.windowDesc.height = 1024;
    config.windowDesc.width  = 1024;
#ifdef _WIN32
    Sample::run(config, pRenderer);
#else
    config.argc = (uint32_t)argc;
    config.argv = argv;
    Sample::run(config, pRenderer);
#endif
    return 0;
}
