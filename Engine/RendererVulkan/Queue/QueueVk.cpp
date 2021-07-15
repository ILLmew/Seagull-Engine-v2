#include "StdAfx.h"
#include "QueueVk.h"

#include "Common/System/ILog.h"
#include "RendererVulkan/Renderer/RendererVk.h"
#include "RendererVulkan/RenderContext/RenderContextVk.h"

namespace SG
{

	QueueVk::QueueVk(EQueueType type, EQueuePriority priority, RendererVk* pRenderer)
		:mType(type), mPriority(priority), mpRenderer(pRenderer)
	{
		auto physicalDevice = (VkPhysicalDevice)pRenderer->GetRenderContext()->GetPhysicalDeviceHandle();

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
		vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

		int i = 0;
		for (const auto& queueFamily : queueFamilies)
		{
			bool bFoundQueue = false;
			switch (mType)
			{
				case EQueueType::eNull:
					SG_LOG_ERROR("Invalid Queue Type!");
					break;
				case EQueueType::eGraphic:
					if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
						mIndex = i;
					bFoundQueue = true;
					break;
				case EQueueType::eCompute:
					if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
						mIndex = i;
					bFoundQueue = true;
					break;
				case EQueueType::eTransfer:
					if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
						mIndex = i;
					bFoundQueue = true;
					break;
			}
			if (bFoundQueue)
				break;
			i++;
		}
	}

	QueueVk::~QueueVk()
	{
	}

	SG::EQueueType QueueVk::GetQueueType() const
	{
		return mType;
	}

	bool QueueVk::IsValid() const
	{
		return mIndex.has_value();
	}

	SG::UInt32 QueueVk::GetQueueIndex() const
	{
		return mIndex.value();
	}

	SG::EQueuePriority QueueVk::GetPriority() const
	{
		return mPriority;
	}

	SG::QueueHandle QueueVk::GetQueueHandle()
	{
		auto logicalDevice = (VkDevice)mpRenderer->GetRenderContext()->GetLogicalDeviceHandle();
		// lazy creation
		vkGetDeviceQueue(logicalDevice, mIndex.value(), 0, &mHandle);
		return mHandle;
	}

	bool QueueVk::operator==(const QueueVk& rhs) const
	{
		return mIndex == rhs.mIndex;
	}

	bool QueueVk::operator!=(const QueueVk& rhs) const
	{
		return mIndex != rhs.mIndex;
	}

}