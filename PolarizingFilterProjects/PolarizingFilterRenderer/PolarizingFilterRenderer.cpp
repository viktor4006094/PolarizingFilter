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
#include "PolarizingFilterRenderer.h"

// pi/180
#define DEG_TO_RAD 0.01745329238474369049072265625f


const std::string PolarizingFilterRenderer::skDefaultScene = "Arcade/Arcade.fscene";

void PolarizingFilterRenderer::initDepthPass()
{
    mDepthPass.pProgram = GraphicsProgram::createFromFile("DepthPass.ps.slang", "", "main");
    mDepthPass.pVars = GraphicsVars::create(mDepthPass.pProgram->getReflector());
}

void PolarizingFilterRenderer::initLightingPass()
{
    //mLightingPass.pProgram = GraphicsProgram::createFromFile("ForwardRenderer.slang", "vs", "ps");
    mLightingPass.pProgram = GraphicsProgram::createFromFile("PolarizingFilterRenderer.hlsl", "vs", "ps");
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

void PolarizingFilterRenderer::initShadowPass(uint32_t windowWidth, uint32_t windowHeight)
{
    mShadowPass.pCsm = CascadedShadowMaps::create(mpSceneRenderer->getScene()->getLight(0), 2048, 2048, windowWidth, windowHeight, mpSceneRenderer->getScene()->shared_from_this());
    mShadowPass.pCsm->setFilterMode(CsmFilterEvsm4);
    mShadowPass.pCsm->setVsmLightBleedReduction(0.3f);
    mShadowPass.pCsm->setVsmMaxAnisotropy(4);
    mShadowPass.pCsm->setEvsmBlur(7, 3);
}

void PolarizingFilterRenderer::initSSAO()
{
    mSSAO.pSSAO = SSAO::create(uvec2(1024));
    mSSAO.pApplySSAOPass = FullScreenPass::create("ApplyAO.ps.slang");
    mSSAO.pVars = GraphicsVars::create(mSSAO.pApplySSAOPass->getProgram()->getReflector());

    Sampler::Desc desc;
    desc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mSSAO.pVars->setSampler("gSampler", Sampler::create(desc));
}

void PolarizingFilterRenderer::setSceneSampler(uint32_t maxAniso)
{
    Scene* pScene = mpSceneRenderer->getScene().get();
    Sampler::Desc samplerDesc;
    samplerDesc.setAddressingMode(Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(maxAniso);
    mpSceneSampler = Sampler::create(samplerDesc);
    pScene->bindSampler(mpSceneSampler);
}

void PolarizingFilterRenderer::applyCustomSceneVars(const Scene* pScene, const std::string& filename)
{
    std::string folder = getDirectoryFromFile(filename);

    Scene::UserVariable var = pScene->getUserVariable("sky_box");
    if (var.type == Scene::UserVariable::Type::String) initSkyBox(folder + '/' + var.str);

    var = pScene->getUserVariable("opacity_scale");
    if (var.type == Scene::UserVariable::Type::Double) mOpacityScale = (float)var.d64;
}

void PolarizingFilterRenderer::initScene(SampleCallbacks* pSample, Scene::SharedPtr pScene)
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
        pDirLight->setIntensity(vec3(1, 1, 0.985f) * 10.0f);
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

    mpSceneRenderer = PolarizingFilterRendererSceneRenderer::create(pScene);
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

void PolarizingFilterRenderer::resetScene()
{
    mpSceneRenderer = nullptr;
    mSkyBox.pEffect = nullptr;
}

void PolarizingFilterRenderer::loadModel(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar)
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

void PolarizingFilterRenderer::loadScene(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar)
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

void PolarizingFilterRenderer::initSkyBox(const std::string& name)
{
    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mSkyBox.pSampler = Sampler::create(samplerDesc);
    mSkyBox.pEffect = SkyBox::create(name, true, mSkyBox.pSampler);
    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthFunc(DepthStencilState::Func::Always);
    mSkyBox.pDS = DepthStencilState::create(dsDesc);
}

void PolarizingFilterRenderer::updateLightProbe(const LightProbe::SharedPtr& pLight)
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

void PolarizingFilterRenderer::initAA(SampleCallbacks* pSample)
{
    mTAA.pTAA = TemporalAA::create();
    mpFXAA = FXAA::create();
    applyAaMode(pSample);
}

void PolarizingFilterRenderer::initPostProcess()
{
    mpToneMapper = ToneMapping::create(ToneMapping::Operator::Aces);
}

void PolarizingFilterRenderer::onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext)
{
    mpState = GraphicsState::create();    
    initPostProcess();
    loadScene(pSample, skDefaultScene, true);
}

void PolarizingFilterRenderer::renderSkyBox(RenderContext* pContext)
{
    if (mSkyBox.pEffect)
    {
        PROFILE("skyBox");
        mpState->setDepthStencilState(mSkyBox.pDS);
        mSkyBox.pEffect->render(pContext, mpSceneRenderer->getScene()->getActiveCamera().get());
        mpState->setDepthStencilState(nullptr);
    }
}

void PolarizingFilterRenderer::beginFrame(RenderContext* pContext, Fbo* pTargetFbo, uint64_t frameId)
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

void PolarizingFilterRenderer::endFrame(RenderContext* pContext)
{
    pContext->popGraphicsState();
}

void PolarizingFilterRenderer::postProcess(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("postProcess");    
    mpToneMapper->execute(pContext, mpResolveFbo->getColorTexture(0), pTargetFbo);
}

void PolarizingFilterRenderer::depthPass(RenderContext* pContext)
{
    PROFILE("depthPass");
    if (mEnableDepthPass == false) 
    {
        return;
    }

    mpState->setFbo(mpDepthPassFbo);
    mpState->setProgram(mDepthPass.pProgram);
    pContext->setGraphicsVars(mDepthPass.pVars);
    
    auto renderMode = mControls[EnableTransparency].enabled ? PolarizingFilterRendererSceneRenderer::Mode::Opaque : PolarizingFilterRendererSceneRenderer::Mode::All;
    mpSceneRenderer->setRenderMode(renderMode);
    mpSceneRenderer->renderScene(pContext);
}

void PolarizingFilterRenderer::lightingPass(RenderContext* pContext, Fbo* pTargetFbo)
{
    PROFILE("lightingPass");
    mpState->setProgram(mLightingPass.pProgram);
    mpState->setDepthStencilState(mEnableDepthPass ? mLightingPass.pDsState : nullptr);
    pContext->setGraphicsVars(mLightingPass.pVars);
    ConstantBuffer::SharedPtr pCB = mLightingPass.pVars->getConstantBuffer("PerFrameCB");
    pCB["gOpacityScale"] = mOpacityScale;

    // Polarizing filter
    pCB["gPolarizingFilterAngle"] = mPolarizingFilterAngle*DEG_TO_RAD; // radians
    pCB["gEnablePolarizingFilter"] = mEnablePolarizingFilter;

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
        mpSceneRenderer->setRenderMode(PolarizingFilterRendererSceneRenderer::Mode::All);
        mpSceneRenderer->renderScene(pContext);
    }
    pContext->flush();
    mpState->setDepthStencilState(nullptr);
}

void PolarizingFilterRenderer::renderOpaqueObjects(RenderContext* pContext)
{
    mpSceneRenderer->setRenderMode(PolarizingFilterRendererSceneRenderer::Mode::Opaque);
    mpSceneRenderer->renderScene(pContext);
}

void PolarizingFilterRenderer::renderTransparentObjects(RenderContext* pContext)
{
    mpSceneRenderer->setRenderMode(PolarizingFilterRendererSceneRenderer::Mode::Transparent);
    mpState->setBlendState(mLightingPass.pAlphaBlendBS);
    mpState->setRasterizerState(mLightingPass.pNoCullRS);
    mpSceneRenderer->renderScene(pContext);
    mpState->setBlendState(nullptr);
    mpState->setRasterizerState(nullptr);
}

void PolarizingFilterRenderer::resolveDepthMSAA(RenderContext* pContext)
{
    if (mAAMode == AAMode::MSAA)
    {
        pContext->resolveResource(mpMainFbo->getDepthStencilTexture(), mpResolveFbo->getColorTexture(2));
    }
}

void PolarizingFilterRenderer::resolveMSAA(RenderContext* pContext)
{
    if(mAAMode == AAMode::MSAA)
    {
        PROFILE("resolveMSAA");
        pContext->resolveResource(mpMainFbo->getColorTexture(0), mpResolveFbo->getColorTexture(0));
        pContext->resolveResource(mpMainFbo->getColorTexture(1), mpResolveFbo->getColorTexture(1));
    }
}

void PolarizingFilterRenderer::shadowPass(RenderContext* pContext)
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

void PolarizingFilterRenderer::runTAA(RenderContext* pContext, Fbo::SharedPtr pColorFbo)
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

void PolarizingFilterRenderer::ambientOcclusion(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
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

void PolarizingFilterRenderer::executeFXAA(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("fxaa");
    if(mAAMode == AAMode::FXAA)
    {
        pContext->blit(pTargetFbo->getColorTexture(0)->getSRV(), mpResolveFbo->getRenderTargetView(0));
        mpFXAA->execute(pContext, mpResolveFbo->getColorTexture(0), pTargetFbo);
    }
}

void PolarizingFilterRenderer::onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
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

void PolarizingFilterRenderer::applyCameraPathState()
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

bool PolarizingFilterRenderer::onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent)
{
    if (mpSceneRenderer && keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        switch (keyEvent.key)
        {
        case KeyboardEvent::Key::Minus:
            mUseCameraPath = !mUseCameraPath;
            applyCameraPathState();
            return true;
        case KeyboardEvent::Key::M:
            mHideMenus = !mHideMenus;
            pSample->toggleGlobalUI(!mHideMenus);
            return true;
        case KeyboardEvent::Key::H:
            gProfileEnabled = true;
            pSample->freezeTime(false);
            return true;
        case KeyboardEvent::Key::O:
            mPerMaterialShader = !mPerMaterialShader;
            mpSceneRenderer->toggleStaticMaterialCompilation(mPerMaterialShader);
            return true;
        }
    }

    return mpSceneRenderer ? mpSceneRenderer->onKeyEvent(keyEvent) : false;
}

void PolarizingFilterRenderer::onDroppedFile(SampleCallbacks* pSample, const std::string& filename)
{
    if (hasSuffix(filename, ".fscene", false) == false)
    {
        msgBox("You can only drop a scene file into the window");
        return;
    }
    loadScene(pSample, filename, true);
}

bool PolarizingFilterRenderer::onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent)
{
    return mpSceneRenderer ? mpSceneRenderer->onMouseEvent(mouseEvent) : true;
}

void PolarizingFilterRenderer::onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height)
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

void PolarizingFilterRenderer::applyCsSkinningMode()
{
    if(mpSceneRenderer)
    {
        SkinningCache::SharedPtr pCache = mUseCsSkinning ? SkinningCache::create() : nullptr;
        mpSceneRenderer->getScene()->attachSkinningCacheToModels(pCache);
    }    
}

void PolarizingFilterRenderer::setActiveCameraAspectRatio(uint32_t w, uint32_t h)
{
    mpSceneRenderer->getScene()->getActiveCamera()->setAspectRatio((float)w / (float)h);
}

#ifdef _WIN32
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
#else
int main(int argc, char** argv)
#endif
{
    PolarizingFilterRenderer::UniquePtr pRenderer = std::make_unique<PolarizingFilterRenderer>();
    SampleConfig config;
    config.windowDesc.title = "Polarizing Filter Renderer";
    config.windowDesc.resizableWindow = false;

    //config.windowDesc.height = 1024;
    //config.windowDesc.width  = 1024;
    config.windowDesc.height = 1080;
    config.windowDesc.width  = 1920;
#ifdef _WIN32
    Sample::run(config, pRenderer);
#else
    config.argc = (uint32_t)argc;
    config.argv = argv;
    Sample::run(config, pRenderer);
#endif
    return 0;
}
