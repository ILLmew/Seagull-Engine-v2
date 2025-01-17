#pragma once

#include "Render/FrameBuffer.h"
#include "Render/GUI/IGUIDriver.h"

#include "RendererVulkan/RenderGraph/RenderGraphNode.h"

#include "Stl/SmartPtr.h"

namespace SG
{

	class VulkanContext;
	class VulkanRenderPass;

	class VulkanPipelineSignature;
	class VulkanPipeline;
	class VulkanShader;

	class RGFinalOutputNode final : public RenderGraphNode
	{
	public:
		RGFinalOutputNode(VulkanContext& context, RenderGraph* pRenderGraph);
		~RGFinalOutputNode();
	private:
		virtual void Reset() override;
		virtual void Prepare(VulkanRenderPass* pRenderpass) override;
		virtual void Draw(DrawInfo& context) override;
	private:
		void GUIDraw(VulkanCommandBuffer& pBuf, UInt32 frameIndex);
	private:
		VulkanContext&    mContext;

		LoadStoreClearOp  mColorRtLoadStoreOp;

		// draw composition pipeline
		//RefPtr<VulkanPipelineSignature> mpCompPipelineSignature;
		//VulkanPipeline* mpCompPipeline;
		//RefPtr<VulkanShader> mpCompShader;

		// draw gui pipeline
		RefPtr<VulkanPipelineSignature> mpGUIPipelineSignature;
		VulkanPipeline* mpGUIPipeline;
		RefPtr<VulkanShader> mpGUIShader;

		UInt32 mCurrVertexCount;
		UInt32 mCurrIndexCount;
	};

}