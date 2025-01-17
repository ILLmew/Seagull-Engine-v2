#pragma once

#include "Render/Pipeline.h"
#include "Render/SwapChain.h"
#include "Render/Queue.h"
#include "Render/Descriptor.h"
#include "Render/ResourceBarriers.h"
#include "Render/Buffer.h"
#include "Render/Shader/Shader.h"
#include "Render/FrameBuffer.h"
#include "Render/Query.h"
 
#include "RendererVulkan/Backend/VulkanAllocator.h"

#include "volk.h"

namespace SG
{

	VkQueryPipelineStatisticFlags ToVkQueryPipelineStatisticsFlag(EPipelineStageQueryType type);

	VmaMemoryUsage ToVmaMemoryUsage(EGPUMemoryUsage usage);

	VkPipelineStageFlagBits ToVKPipelineStageFlags(EPipelineStage stage);
	VkAccessFlags ToVKAccessFlags(EPipelineStageAccess stage);

	VkVertexInputRate ToVkVertexInputRate(EVertexInputRate vertexip);
	VkPolygonMode ToVkPolygonMode(EPolygonMode polymode);
	VkCullModeFlags ToVkCullMode(ECullMode cullmode);

	VkImageLayout ToVkImageLayout(EImageLayout layout);

	VkFilter ToVkFilterMode(EFilterMode fm);
	VkSamplerMipmapMode  ToVkMipmapMode(EFilterMode fm);
	VkSamplerAddressMode ToVkAddressMode(EAddressMode am);

	VkAttachmentLoadOp ToVkLoadOp(ELoadOp  op);
	VkAttachmentStoreOp ToVkStoreOp(EStoreOp op);

	VkFormat     ToVkImageFormat(EImageFormat format);
	EImageFormat ToSGImageFormat(VkFormat format);

	VkDescriptorType ToVkDescriptorType(EDescriptorType type);
	VkDescriptorType ToVkDescriptorType(EImageUsage usage);

	VkShaderStageFlags ToVkShaderStageFlags(EShaderStage stage);

	VkFormat     ToVkShaderDataFormat(EShaderDataType type);

	VkImageType  ToVkImageType(EImageType type);
	EImageType   ToSGImageType(VkImageType type);

	VkSampleCountFlagBits ToVkSampleCount(ESampleCount cnt);
	ESampleCount ToSGSampleCount(VkSampleCountFlagBits cnt);

	VkImageUsageFlags  ToVkImageUsage(EImageUsage type);
	EImageUsage ToSGImageUsage(VkImageUsageFlags usage);

	VkBufferUsageFlags ToVkBufferUsage(EBufferType type);

	VkPresentModeKHR ToVkPresentMode(EPresentMode pmode);

	VkAccessFlags ToVkAccessFlags(EResourceBarrier barrier);
	VkImageLayout ToVkImageLayout(EResourceBarrier barrier);

	VkImageViewType ToVkImageViewType(VkImageType imageType, UInt32 array);

	VkPipelineStageFlags ToVkPipelineStageFlags(VkAccessFlags accessFlags, EQueueType queueType);

}