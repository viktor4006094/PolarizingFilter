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
#pragma once
#include "Falcor.h"

using namespace Falcor;

class MaterialDemoRendererSceneRenderer : public SceneRenderer
{
public:
    using SharedPtr = std::shared_ptr<MaterialDemoRendererSceneRenderer>;
    ~MaterialDemoRendererSceneRenderer() = default;
    enum class Mode
    {
        All,
        Opaque,
        Transparent
    };

    static SharedPtr create(const Scene::SharedPtr& pScene);
    void setRenderMode(Mode renderMode) { mRenderMode = renderMode; }
    void renderScene(RenderContext* pContext) override;
private:
    bool setPerMeshData(const CurrentWorkingData& currentData, const Mesh* pMesh) override;
    bool setPerMaterialData(const CurrentWorkingData& currentData, const Material* pMaterial) override;
    RasterizerState::SharedPtr getRasterizerState(const Material* pMaterial);
	MaterialDemoRendererSceneRenderer(const Scene::SharedPtr& pScene);
    std::vector<bool> mTransparentMeshes;
    Mode mRenderMode = Mode::All;
    bool mHasOpaqueObjects = false;
    bool mHasTransparentObject = false;

    RasterizerState::SharedPtr mpDefaultRS;
    RasterizerState::SharedPtr mpNoCullRS;
    RasterizerState::SharedPtr mpLastSetRs;
};
