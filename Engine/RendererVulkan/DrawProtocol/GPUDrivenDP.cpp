#include "StdAfx.h"
#include "GPUDrivenDP.h"

#include "System/System.h"
#include "Archive/MeshDataArchive.h"
#include "Render/Shader/ShaderComiler.h"
#include "Profile/Profile.h"

#include "RendererVulkan/Backend/VulkanContext.h"
#include "RendererVulkan/Backend/VulkanCommand.h"
#include "RendererVulkan/Backend/VulkanQueue.h"
#include "RendererVulkan/Backend/VulkanShader.h"
#include "RendererVulkan/Backend/VulkanPipelineSignature.h"
#include "RendererVulkan/Backend/VulkanPipeline.h"
#include "RendererVulkan/Backend/VulkanSynchronizePrimitive.h"

#include "RendererVulkan/Resource/RenderResourceRegistry.h"

namespace SG
{

	VulkanContext* GPUDrivenDP::mpContext = nullptr;
	VulkanCommandBuffer* GPUDrivenDP::mpCmdBuf = nullptr;
	UInt32 GPUDrivenDP::mCurrFrameIndex = 0;

	//VulkanQueryPool* GPUDrivenDP::pComputeResetQueryPool = nullptr;
	//VulkanQueryPool* GPUDrivenDP::pComputeCullingQueryPool = nullptr;

	eastl::fixed_map<EMeshPass, vector<IndirectDrawCall>, (UInt32)EMeshPass::NUM_MESH_PASS> GPUDrivenDP::mDrawCallMap;
	UInt32 GPUDrivenDP::mPackedVBCurrOffset = 0;
	UInt32 GPUDrivenDP::mPackedVIBCurrOffset = 0;
	UInt32 GPUDrivenDP::mPackedIBCurrOffset = 0;
	UInt32 GPUDrivenDP::mCurrDrawCallIndex = 0;

	vector<VulkanCommandBuffer> GPUDrivenDP::mResetCommands;
	vector<VulkanCommandBuffer> GPUDrivenDP::mCullingCommands;
	vector<VulkanCommandBuffer> GPUDrivenDP::mTransferCommands;
	vector<VulkanFence*> GPUDrivenDP::mTransferFences;
	VulkanSemaphore* GPUDrivenDP::mpWaitResetSemaphore = nullptr;

	RefPtr<VulkanPipelineSignature> GPUDrivenDP::mpResetCullingPipelineSignature = nullptr;
	VulkanPipeline*                 GPUDrivenDP::mpResetCullingPipeline = nullptr;
	RefPtr<VulkanShader>            GPUDrivenDP::mpResetCullingShader = nullptr;

	RefPtr<VulkanPipelineSignature> GPUDrivenDP::mpGPUCullingPipelineSignature = nullptr;
	VulkanPipeline*                 GPUDrivenDP::mpGPUCullingPipeline = nullptr;
	RefPtr<VulkanShader>            GPUDrivenDP::mpGPUCullingShader = nullptr;

	RefPtr<VulkanPipelineSignature> GPUDrivenDP::mpDrawCallCompactPipelineSignature = nullptr;
	VulkanPipeline*                 GPUDrivenDP::mpDrawCallCompactPipeline = nullptr;
	RefPtr<VulkanShader>            GPUDrivenDP::mpDrawCallCompactShader = nullptr;

	bool GPUDrivenDP::mbRendererInit = false;
	bool GPUDrivenDP::mbBeginDraw = false;
	bool GPUDrivenDP::mbDrawCallReady = false;

	void GPUDrivenDP::OnInit(VulkanContext& context)
	{
		SG_PROFILE_FUNCTION();

		mpContext = &context;

		BufferCreateDesc indirectCI = {};
		indirectCI.name = "indirectBuffer";
		indirectCI.bufferSize = sizeof(DrawIndexedIndirectCommand) * SG_MAX_DRAW_CALL;
		indirectCI.type = EBufferType::efIndirect | EBufferType::efStorage | EBufferType::efTransfer_Src;
		indirectCI.memoryUsage = EGPUMemoryUsage::eGPU_Only;
		if (!VK_RESOURCE()->CreateBuffer(indirectCI))
			return;

		BufferCreateDesc indirectReadBackCI = {};
		indirectReadBackCI.name = "indirectBuffer_read_back";
		indirectReadBackCI.bufferSize = sizeof(DrawIndexedIndirectCommand) * SG_MAX_DRAW_CALL;
		indirectReadBackCI.type = EBufferType::efStorage | EBufferType::efTransfer_Dst;
		indirectReadBackCI.memoryUsage = EGPUMemoryUsage::eGPU_To_CPU;
		indirectReadBackCI.memoryFlag = EGPUMemoryFlag::efPersistent_Map;
		if (!VK_RESOURCE()->CreateBuffer(indirectReadBackCI))
			return;

		// create one big vertex buffer
		BufferCreateDesc vibCI = {};
		vibCI.name = "instanceBuffer";
		vibCI.bufferSize = SG_MAX_PACKED_INSTANCE_BUFFER_SIZE;
		vibCI.type = EBufferType::efVertex | EBufferType::efStorage;
		vibCI.memoryUsage = EGPUMemoryUsage::eGPU_Only;
		if (!VK_RESOURCE()->CreateBuffer(vibCI))
			return;

#if SG_ENABLE_GPU_CULLING
		BufferCreateDesc insOutCI = {};
		insOutCI.name = "instanceOutput";
		insOutCI.bufferSize = SG_MAX_NUM_OBJECT * sizeof(InstanceOutputData);
		insOutCI.type = EBufferType::efStorage;
		insOutCI.memoryUsage = EGPUMemoryUsage::eGPU_Only;
		if (!VK_RESOURCE()->CreateBuffer(insOutCI))
			return;

		BufferCreateDesc instanceSumTableCI = {};
		instanceSumTableCI.name = "instanceSumTable";
		instanceSumTableCI.bufferSize = sizeof(UInt32) * SG_MAX_DRAW_CALL;
		instanceSumTableCI.type = EBufferType::efStorage;
		instanceSumTableCI.memoryUsage = EGPUMemoryUsage::eGPU_Only;
		if (!VK_RESOURCE()->CreateBuffer(instanceSumTableCI))
			return;

		mpGPUCullingShader = VulkanShader::Create(mpContext->device);
		mpDrawCallCompactShader = VulkanShader::Create(mpContext->device);
		mpResetCullingShader = VulkanShader::Create(mpContext->device);
		ShaderCompiler compiler;
		compiler.CompileGLSLShader("culling/culling", mpGPUCullingShader);
		compiler.CompileGLSLShader("culling/drawcall_compact", mpDrawCallCompactShader);
		compiler.CompileGLSLShader("culling/culling_reset", mpResetCullingShader);

		mpWaitResetSemaphore = VulkanSemaphore::Create(mpContext->device);
		mTransferFences.resize(mpContext->pSwapchain->imageCount);
		for (Size i = 0; i < mpContext->pSwapchain->imageCount; ++i)
		{
			VulkanFence** ppFence = &mTransferFences[i];
			*ppFence = VulkanFence::Create(mpContext->device);
		}

		mResetCommands.resize(mpContext->pSwapchain->imageCount);
		for (auto& cmd : mResetCommands)
			mpContext->pComputeCommandPool->AllocateCommandBuffer(cmd);
		mCullingCommands.resize(mpContext->pSwapchain->imageCount);
		for (auto& cmd : mCullingCommands)
			mpContext->pComputeCommandPool->AllocateCommandBuffer(cmd);
		mTransferCommands.resize(mpContext->pSwapchain->imageCount);
		for (auto& cmd : mTransferCommands)
			mpContext->pTransferCommandPool->AllocateCommandBuffer(cmd);
#endif

		//pComputeResetQueryPool = VulkanQueryPool::Create(mpContext->device, ERenderQueryType::ePipeline_Statistics, EPipelineStageQueryType::efCompute_Shader_Invocations);
		//pComputeCullingQueryPool = VulkanQueryPool::Create(mpContext->device, ERenderQueryType::ePipeline_Statistics, EPipelineStageQueryType::efCompute_Shader_Invocations);
		mbRendererInit = true;
	}

	void GPUDrivenDP::OnShutdown()
	{
		SG_PROFILE_FUNCTION();

		//Memory::Delete(pComputeResetQueryPool);
		//Memory::Delete(pComputeCullingQueryPool);

#if SG_ENABLE_GPU_CULLING
		mpResetCullingShader.reset();
		mpResetCullingPipelineSignature.reset();
		Delete(mpResetCullingPipeline);

		mpDrawCallCompactShader.reset();
		mpDrawCallCompactPipelineSignature.reset();
		Delete(mpDrawCallCompactPipeline);

		mpGPUCullingShader.reset();
		mpGPUCullingPipelineSignature.reset();
		Delete(mpGPUCullingPipeline);

		for (auto* pFence : mTransferFences)
			Delete(pFence);
		for (auto& cmd : mTransferCommands)
			mpContext->pTransferCommandPool->FreeCommandBuffer(cmd);
		for (auto& cmd : mResetCommands)
			mpContext->pComputeCommandPool->FreeCommandBuffer(cmd);
		for (auto& cmd : mCullingCommands)
			mpContext->pComputeCommandPool->FreeCommandBuffer(cmd);
		Delete(mpWaitResetSemaphore);

		VK_RESOURCE()->DeleteBuffer("instanceOutput");
		VK_RESOURCE()->DeleteBuffer("instanceSumTable");
#endif

		VK_RESOURCE()->DeleteBuffer("indirectBuffer");
		VK_RESOURCE()->DeleteBuffer("indirectBuffer_read_back");

		VK_RESOURCE()->DeleteBuffer("instanceBuffer");
		VK_RESOURCE()->DeleteBuffer("packed_vertex_buffer_0");
		VK_RESOURCE()->DeleteBuffer("packed_index_buffer_0");

		mDrawCallMap.clear();

		mCurrDrawCallIndex = 0;
		mPackedVBCurrOffset = 0;
		mPackedVIBCurrOffset = 0;
		mPackedIBCurrOffset = 0;

		mpCmdBuf = nullptr;
		mpContext = nullptr;

		mbRendererInit = false;
	}

	void GPUDrivenDP::CollectRenderData(RefPtr<RenderDataBuilder> pRenderDataBuilder)
	{
		SG_PROFILE_FUNCTION();

		// clear all the old data and reset status
		mDrawCallMap.clear();
		mCurrDrawCallIndex = 0;
		mPackedVBCurrOffset = 0;
		mPackedVIBCurrOffset = 0;
		mPackedIBCurrOffset = 0;

		if (!mbRendererInit)
		{
			SG_LOG_ERROR("Renderer not initialized");
			return;
		}

		vector<UInt32> instanceSumTable;
		for (UInt32 i = 0; i < MeshDataArchive::GetInstance()->GetNumMeshData(); ++i)
			instanceSumTable.emplace_back(MeshDataArchive::GetInstance()->GetInstanceSumOffset(i));

		BufferCreateDesc instanceSumTableCI = {};
		instanceSumTableCI.name = "instanceSumTable";
		instanceSumTableCI.bufferSize = sizeof(UInt32) * SG_MAX_DRAW_CALL;
		instanceSumTableCI.type = EBufferType::efStorage;
		instanceSumTableCI.memoryUsage = EGPUMemoryUsage::eGPU_Only;
		instanceSumTableCI.pInitData = instanceSumTable.data();
		instanceSumTableCI.subBufferSize = static_cast<UInt32>(sizeof(UInt32) * instanceSumTable.size());
		instanceSumTableCI.subBufferOffset = 0;
		instanceSumTableCI.bSubBuffer = true;
		if (!VK_RESOURCE()->CreateBuffer(instanceSumTableCI))
			return;

		vector<InstanceOutputData> instanceOutputData;
		vector<DrawIndexedIndirectCommand> indirectCommands;
		indirectCommands.resize(MeshDataArchive::GetInstance()->GetNumMeshData());

		auto pScene = SSystem()->GetMainScene();
		pRenderDataBuilder->TraverseRenderData([&](UInt32 meshId, const RendererBuildData& buildData)
			{
				if (buildData.instanceCount > 1) // move it to the Forward Instance Mesh Pass
				{
					const UInt64 ivbSize = sizeof(PerInstanceData) * buildData.instanceCount;
					if (mPackedVIBCurrOffset + ivbSize > SG_MAX_PACKED_INSTANCE_BUFFER_SIZE)
					{
						SG_LOG_ERROR("Instance buffer exceed boundary!");
						SG_ASSERT(false);
					}

					// create one big vertex buffer
					BufferCreateDesc vibCI = {};
					vibCI.name = "instanceBuffer";
					vibCI.bufferSize = SG_MAX_PACKED_INSTANCE_BUFFER_SIZE;
					vibCI.type = EBufferType::efVertex | EBufferType::efStorage;
					vibCI.memoryUsage = EGPUMemoryUsage::eGPU_Only;
					vibCI.pInitData = buildData.perInstanceData.data();
					vibCI.subBufferSize = static_cast<UInt32>(ivbSize);
					vibCI.subBufferOffset = static_cast<UInt32>(mPackedVIBCurrOffset);
					vibCI.bSubBuffer = true;
					VK_RESOURCE()->CreateBuffer(vibCI);
					SG_LOG_DEBUG("Have instance!");

					for (UInt32 i = 0; i < buildData.instanceCount; ++i)
					{
						InstanceOutputData insOutputData = {};
						insOutputData.objectId = buildData.perInstanceData[i].objectId;
						instanceOutputData.emplace_back(insOutputData);
					}
				}

				auto* pMeshData = MeshDataArchive::GetInstance()->GetData(meshId);
				const UInt64 vbSize = pMeshData->vertices.size() * sizeof(float);
				const UInt64 ibSize = pMeshData->indices.size() * sizeof(UInt32);

				if (mPackedVBCurrOffset + vbSize > SG_MAX_PACKED_VERTEX_BUFFER_SIZE)
				{
					SG_LOG_ERROR("Vertex buffer exceed boundary!");
					SG_ASSERT(false);
				}
				if (mPackedIBCurrOffset + ibSize > SG_MAX_PACKED_INDEX_BUFFER_SIZE)
				{
					SG_LOG_ERROR("Index buffer exceed boundary!");
					SG_ASSERT(false);
				}

				// create one big vertex buffer
				BufferCreateDesc vbCI = {};
				vbCI.name = "packed_vertex_buffer_0";
				vbCI.bufferSize = SG_MAX_PACKED_VERTEX_BUFFER_SIZE;
				vbCI.type = EBufferType::efVertex;
				vbCI.memoryUsage = EGPUMemoryUsage::eGPU_Only;
				vbCI.pInitData = pMeshData->vertices.data();
				vbCI.subBufferSize = static_cast<UInt32>(vbSize);
				vbCI.subBufferOffset = static_cast<UInt32>(mPackedVBCurrOffset);
				vbCI.bSubBuffer = true;
				VK_RESOURCE()->CreateBuffer(vbCI);

				// create one big index buffer
				BufferCreateDesc ibCI = {};
				ibCI.name = "packed_index_buffer_0";
				ibCI.bufferSize = SG_MAX_PACKED_INDEX_BUFFER_SIZE;
				ibCI.type = EBufferType::efIndex;
				ibCI.memoryUsage = EGPUMemoryUsage::eGPU_Only;
				ibCI.pInitData = pMeshData->indices.data();
				ibCI.subBufferSize = static_cast<UInt32>(ibSize);
				ibCI.subBufferOffset = static_cast<UInt32>(mPackedIBCurrOffset);
				ibCI.bSubBuffer = true;
				VK_RESOURCE()->CreateBuffer(ibCI);
				VK_RESOURCE()->FlushBuffers();

				IndirectDrawCall indirectDc;
				indirectDc.drawMesh.pVertexBuffer = VK_RESOURCE()->GetBuffer("packed_vertex_buffer_0");
				indirectDc.drawMesh.vBSize = vbSize;
				indirectDc.drawMesh.vBOffset = mPackedVBCurrOffset;
				indirectDc.drawMesh.pIndexBuffer = VK_RESOURCE()->GetBuffer("packed_index_buffer_0");;
				indirectDc.drawMesh.iBSize = ibSize;
				indirectDc.drawMesh.iBOffset = mPackedIBCurrOffset;
				indirectDc.drawMesh.pInstanceBuffer = nullptr;
				indirectDc.drawMesh.instanceOffset = 0;

				auto* pPipelineSignature = mpContext->pTempPipelineSignature;
				indirectDc.drawMaterial.pPipelineSignature = pPipelineSignature;
				VulkanPipelineSignature::ShaderDataBinder setBinder(pPipelineSignature, mpContext->pShader, 1);
				setBinder.AddCombindSamplerImage("texture_1k_mipmap_sampler", buildData.albedoTexAssetName.c_str());
				setBinder.AddCombindSamplerImage("texture_1k_mipmap_sampler", buildData.metallicTexAssetName.c_str());
				setBinder.AddCombindSamplerImage("texture_1k_mipmap_sampler", buildData.roughnessTexAssetName.c_str());
				setBinder.AddCombindSamplerImage("texture_1k_mipmap_sampler", buildData.aoTexAssetName.c_str());
				setBinder.AddCombindSamplerImage("texture_1k_mipmap_sampler", buildData.normalTexAssetName.c_str());
				auto& descriptorSet = setBinder.BindNew(buildData.materialAssetName);

				indirectDc.drawMaterial.materialAssetName = buildData.materialAssetName;

				indirectDc.count = 1;
				indirectDc.first = meshId;
				indirectDc.pIndirectBuffer = VK_RESOURCE()->GetBuffer("indirectBuffer");

				if (buildData.instanceCount == 1)
				{
					mDrawCallMap[EMeshPass::eForward].emplace_back(eastl::move(indirectDc));
				}
				else
				{
					indirectDc.drawMesh.pInstanceBuffer = VK_RESOURCE()->GetBuffer("instanceBuffer");
					indirectDc.drawMesh.instanceOffset = mPackedVIBCurrOffset;
					mPackedVIBCurrOffset += static_cast<UInt32>(sizeof(PerInstanceData) * buildData.instanceCount);
					mDrawCallMap[EMeshPass::eForwardInstanced].emplace_back(eastl::move(indirectDc));
				}

				DrawIndexedIndirectCommand indirect;
				//indirect.firstIndex = mPackedIBCurrOffset / sizeof(UInt32);
				indirect.firstIndex = 0;
				indirect.firstInstance = buildData.instanceCount == 1 ? buildData.objectId : 0;
				indirect.indexCount = static_cast<UInt32>(ibSize / sizeof(UInt32));
				indirect.instanceCount = buildData.instanceCount;
				indirect.vertexOffset = 0;
				//indirect.vertexOffset = mPackedVBCurrOffset / (sizeof(float) * 6);

				indirectCommands[meshId] = eastl::move(indirect);

				mPackedVBCurrOffset += static_cast<UInt32>(vbSize);
				mPackedIBCurrOffset += static_cast<UInt32>(ibSize);
			});

		//SG_ASSERT(instanceOutputData.size() == SSystem()->GetMainScene()->GetMeshEntityCount() + 1);

		BufferCreateDesc indirectCI = {};
		indirectCI.name = "indirectBuffer";
		indirectCI.bufferSize = sizeof(DrawIndexedIndirectCommand) * SG_MAX_DRAW_CALL;
		indirectCI.type = EBufferType::efIndirect | EBufferType::efStorage;
		indirectCI.memoryUsage = EGPUMemoryUsage::eGPU_Only;
		indirectCI.pInitData = indirectCommands.data();
		indirectCI.subBufferSize = static_cast<UInt32>(sizeof(DrawIndexedIndirectCommand) * indirectCommands.size());
		indirectCI.subBufferOffset = 0;
		indirectCI.bSubBuffer = true;
		if (!VK_RESOURCE()->CreateBuffer(indirectCI))
			return;

		// resolve instance outputData
		//pScene->TraverseEntity([](auto& entity)
		//	{
		//		if (entity.HasComponent<MeshComponent>())
		//		{
		//			MeshComponent& mesh = entity.GetComponent<MeshComponent>();
		//		}
		//	});

#if SG_ENABLE_GPU_CULLING
		if (!instanceOutputData.empty())
		{
			BufferCreateDesc insOutputCI = {};
			insOutputCI.name = "instanceOutput";
			insOutputCI.type = EBufferType::efStorage;
			insOutputCI.bufferSize = SG_MAX_NUM_OBJECT * sizeof(InstanceOutputData);
			insOutputCI.memoryUsage = EGPUMemoryUsage::eGPU_Only;
			insOutputCI.pInitData = instanceOutputData.data();
			insOutputCI.subBufferSize = static_cast<UInt32>(sizeof(InstanceOutputData) * instanceOutputData.size());
			insOutputCI.subBufferOffset = 0;
			insOutputCI.bSubBuffer = true;
			VK_RESOURCE()->CreateBuffer(insOutputCI);
		}
#endif
		VK_RESOURCE()->FlushBuffers();

#if SG_ENABLE_GPU_CULLING
		if (!mbDrawCallReady)
		{
			mpResetCullingPipelineSignature = VulkanPipelineSignature::Builder(*mpContext, mpResetCullingShader)
				.Build();

			mpResetCullingPipeline = VulkanPipeline::Builder(mpContext->device, EPipelineType::eCompute)
				.BindSignature(mpResetCullingPipelineSignature)
				.Build();

			mpGPUCullingPipelineSignature = VulkanPipelineSignature::Builder(*mpContext, mpGPUCullingShader)
				.Build();

			mpGPUCullingPipeline = VulkanPipeline::Builder(mpContext->device, EPipelineType::eCompute)
				.BindSignature(mpGPUCullingPipelineSignature)
				.Build();

			mpDrawCallCompactPipelineSignature = VulkanPipelineSignature::Builder(*mpContext, mpDrawCallCompactShader)
				.Build();

			mpDrawCallCompactPipeline = VulkanPipeline::Builder(mpContext->device, EPipelineType::eCompute)
				.BindSignature(mpDrawCallCompactPipelineSignature)
				.Build();
		}
#endif

		LogDebugInfo();

		mbDrawCallReady = true;
	}

	void GPUDrivenDP::Begin(DrawInfo& drawInfo)
	{
		SG_PROFILE_FUNCTION();

		if (mbBeginDraw)
		{
			SG_LOG_ERROR("Did you forget to call End() after drawing?");
			return;
		}
		if (!mbDrawCallReady)
		{
			SG_LOG_WARN("Please call CollectRenderData() before your drawing!");
			return;
		}

		mbBeginDraw = true;
		mpCmdBuf = drawInfo.pCmd;
		mCurrFrameIndex = drawInfo.frameIndex;
	}

	void GPUDrivenDP::End()
	{
		SG_PROFILE_FUNCTION();

		if (!mbBeginDraw)
		{
			SG_LOG_ERROR("Did you forget to call Begin() before drawing?");
			return;
		}

		mbBeginDraw = false;
		mpCmdBuf = nullptr;
		mCurrFrameIndex = UInt32(-1);
	}

	void GPUDrivenDP::CullingReset()
	{
		// [Critical] Here discovered severe performance fluctuation.
		SG_PROFILE_FUNCTION();

#if SG_ENABLE_GPU_CULLING
		// Why adding a query pool will fail the submittion of the command?
		//auto& statistics = GetStatisticData();

		auto& cmd = mResetCommands[mCurrFrameIndex];
		cmd.Reset();
		cmd.BeginRecord();
		{
			//mResetCommand.ResetQueryPool(pComputeResetQueryPool);
			//mResetCommand.BeginQuery(pComputeResetQueryPool, 0);

			cmd.BindPipeline(mpResetCullingPipeline);
			cmd.BindPipelineSignatureNonDynamic(mpResetCullingPipelineSignature.get(), EPipelineType::eCompute);
			UInt32 numGroup = (UInt32)(MeshDataArchive::GetInstance()->GetNumMeshData() / 16) + 1;
			cmd.Dispatch(numGroup, 1, 1);

			//mResetCommand.EndQuery(pComputeResetQueryPool, 0);
		}
		cmd.EndRecord();

		mpContext->pComputeQueue->SubmitCommands<0, 1, 0>(&cmd, nullptr, &mpWaitResetSemaphore, nullptr, nullptr);
		//statistics.pipelineStatistics[6] = *pComputeResetQueryPool->GetQueryResult(0, 1);
#endif
	}

	void GPUDrivenDP::DoCulling()
	{
		// [Critical] Here discovered severe performance fluctuation.
		SG_PROFILE_FUNCTION();

#if SG_ENABLE_GPU_CULLING
		auto& cmd = mCullingCommands[mCurrFrameIndex];
		cmd.Reset();
		cmd.BeginRecord();
		{
			//cmd.ResetQueryPool(pComputeCullingQueryPool);
			//cmd.BeginQuery(pComputeCullingQueryPool, 0);

			//cmd.BufferBarrier(VK_RESOURCE()->GetBuffer("indirectBuffer"), EPipelineStageAccess::efIndirect_Read, EPipelineStageAccess::efShader_Write,
			//	EPipelineType::eGraphic, EPipelineType::eCompute);

			// [Data Hazard] barrier to prevent instanceOutput RAW(Read After Write) scenario
			cmd.BufferBarrier(VK_RESOURCE()->GetBuffer("instanceOutput"), EPipelineStageAccess::efShader_Read, EPipelineStageAccess::efShader_Write,
				EPipelineType::eCompute, EPipelineType::eCompute);

			cmd.BindPipeline(mpGPUCullingPipeline);
			cmd.BindPipelineSignatureNonDynamic(mpGPUCullingPipelineSignature.get(), EPipelineType::eCompute);
			UInt32 numGroup = (UInt32)(SSystem()->GetMainScene()->GetMeshEntityCount() / 128) + 1;
			cmd.Dispatch(numGroup, 1, 1);

			cmd.BufferBarrier(VK_RESOURCE()->GetBuffer("instanceOutput"), EPipelineStageAccess::efShader_Write, EPipelineStageAccess::efShader_Read,
				EPipelineType::eCompute, EPipelineType::eCompute);

			cmd.BindPipeline(mpDrawCallCompactPipeline);
			cmd.BindPipelineSignatureNonDynamic(mpDrawCallCompactPipelineSignature.get(), EPipelineType::eCompute);
			cmd.Dispatch(1, 1, 1);

			// We use semaphore to ensure that out compute command is finish before the graphic queue begin to draw,
			// So i think we don't need the buffer barrier here!
		 
			//cmd.BufferBarrier(VK_RESOURCE()->GetBuffer("indirectBuffer"), EPipelineStageAccess::efShader_Write, EPipelineStageAccess::efIndirect_Read,
			//	EPipelineType::eCompute, EPipelineType::eGraphic);

			//cmd.EndQuery(pComputeCullingQueryPool, 0);
		}
		cmd.EndRecord();

		// submit compute commands
		// semaphore used to ensure the sequence of GPU-side execution
		// fence used to ensure CPU is not write to a pending status command buffer. (because here we only have one compute command buffer)
		EPipelineStage waitStage[1] = { EPipelineStage::efTop_Of_Pipeline };
		mpContext->pComputeQueue->SubmitCommands<1, 1, 1>(&cmd, waitStage, &mpContext->pComputeCompleteSemaphore, 
			&mpWaitResetSemaphore, mpContext->pComputeSyncFences[mCurrFrameIndex]);
		//statistics.pipelineStatistics[7] = *pComputeCullingQueryPool->GetQueryResult(0, 1);
#endif
	}

	void GPUDrivenDP::CopyStatisticsData()
	{
		SG_PROFILE_FUNCTION();

		auto& cmd = mTransferCommands[mCurrFrameIndex];

		cmd.Reset();
		cmd.BeginRecord();
		{
			cmd.CopyBuffer(*VK_RESOURCE()->GetBuffer("indirectBuffer"), *VK_RESOURCE()->GetBuffer("indirectBuffer_read_back"), 0, 0);
		}
		cmd.EndRecord();

		EPipelineStage waitStage[1] = { EPipelineStage::efCompute_Shader };
		mpContext->pTransferQueue->SubmitCommands<1, 0, 1>(&cmd, waitStage, nullptr, &mpContext->pComputeCompleteSemaphore, mTransferFences[mCurrFrameIndex]);
	}

	void GPUDrivenDP::WaitForStatisticsCopyed()
	{
#if SG_ENABLE_GPU_CULLING
		mTransferFences[mCurrFrameIndex]->WaitAndReset();
#endif
	}

	void GPUDrivenDP::Draw(EMeshPass meshPass)
	{
		SG_PROFILE_FUNCTION();

		for (auto& dc : mDrawCallMap[meshPass])
		{
			BindMesh(dc.drawMesh); // bind vertex buffer, index buffer and instance buffer (if exists)
			BindMaterial(dc.drawMaterial); // bind descriptor set (i.e. bind resources into the pipeline layout)

			mpCmdBuf->DrawIndexedIndirect(dc.pIndirectBuffer, dc.first * sizeof(DrawIndexedIndirectCommand), 1, sizeof(DrawIndexedIndirectCommand));
		}
	}

	void GPUDrivenDP::DrawWithoutBindMaterial(EMeshPass meshPass)
	{
		SG_PROFILE_FUNCTION();

		for (auto& dc : mDrawCallMap[meshPass])
		{
			BindMesh(dc.drawMesh); // bind vertex buffer, index buffer and instance buffer (if exists)

			mpCmdBuf->DrawIndexedIndirect(dc.pIndirectBuffer, dc.first * sizeof(DrawIndexedIndirectCommand), 1, sizeof(DrawIndexedIndirectCommand));
		}
	}

	void GPUDrivenDP::BindMesh(const DrawMesh& drawMesh)
	{
		SG_PROFILE_FUNCTION();

		mpCmdBuf->BindVertexBuffer(0, 1, *drawMesh.pVertexBuffer, &drawMesh.vBOffset);
		if (drawMesh.pInstanceBuffer)
			mpCmdBuf->BindVertexBuffer(1, 1, *drawMesh.pInstanceBuffer, &drawMesh.instanceOffset);
		mpCmdBuf->BindIndexBuffer(*drawMesh.pIndexBuffer, drawMesh.iBOffset);
	}

	void GPUDrivenDP::BindMaterial(const DrawMaterial& drawMaterial)
	{
		SG_PROFILE_FUNCTION();

		mpCmdBuf->BindPipelineSignatureNonDynamic(drawMaterial.pPipelineSignature, 0); // bind necessary resource first
		if (!drawMaterial.materialAssetName.empty())
		{
			auto& descriptorSet = drawMaterial.pPipelineSignature->GetDescriptorSet(1, drawMaterial.materialAssetName);
			mpCmdBuf->BindDescriptorSet(drawMaterial.pPipelineSignature, 1, &descriptorSet); // set index 1 for custom user resource bind.
		}
	}

	void GPUDrivenDP::LogDebugInfo()
	{
		SG_LOG_DEBUG("Indirect Renderer Debug Info:");
		for (auto& dc : mDrawCallMap)
		{
			SG_LOG_DEBUG("Mesh Pass: %d", (UInt32)dc.first);
			for (auto& idc : dc.second)
			{
				SG_LOG_DEBUG("    Draw Call: 0x%p", idc.pIndirectBuffer);
				SG_LOG_DEBUG("    Draw Call Count: %d", idc.count);
				SG_LOG_DEBUG("    Draw Call Offset: %d", idc.first);
			}
		}
	}

}