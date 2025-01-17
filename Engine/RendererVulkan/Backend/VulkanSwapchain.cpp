#include "StdAfx.h"
#include "VulkanSwapchain.h"

#include "System/System.h"
#include "Platform/OS.h"

#include "VulkanContext.h"
#include "VulkanQueue.h"
#include "VulkanTexture.h"
#include "VulkanSynchronizePrimitive.h"
#include "RendererVulkan/Utils/VkConvert.h"

#include "Memory/Memory.h"

namespace SG
{

	VulkanSwapchain::VulkanSwapchain(VulkanContext& c)
		:context(c)
	{
		CreateSurface();
	}

	VulkanSwapchain::~VulkanSwapchain()
	{
		DestroySurface();
	}

	bool VulkanSwapchain::CreateOrRecreate(UInt32 width, UInt32 height, bool vsync)
	{
		// if there is an old swapchain, use it to ease up recreation.
		VkSwapchainKHR oldSwapchain = swapchain;

		VkSurfaceCapabilitiesKHR capabilities = {};
		vector<VkSurfaceFormatKHR> formats;
		vector<VkPresentModeKHR> presentModes;

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.instance.physicalDevice, mPresentSurface, &capabilities);

		UInt32 formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(context.instance.physicalDevice, mPresentSurface, &formatCount, nullptr);
		if (formatCount != 0)
		{
			formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(context.instance.physicalDevice, mPresentSurface, &formatCount, formats.data());
		}

		UInt32 presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(context.instance.physicalDevice, mPresentSurface, &presentModeCount, nullptr);
		if (presentModeCount != 0)
		{
			presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(context.instance.physicalDevice, mPresentSurface, &presentModeCount, presentModes.data());
		}

		// if the swapchain can do presenting
		bool bIsSwapChainAdequate = false;
		bIsSwapChainAdequate = !formats.empty() && !presentModes.empty();
		if (!bIsSwapChainAdequate)
			SG_LOG_WARN("Unpresentable swapchain detected");

		// the VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
		// this mode waits for the vertical blank ("v-sync")
		VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

		// if v-sync is not requested, try to find a mailbox mode
		// it's the lowest latency non-tearing present mode available
		if (!vsync)
		{
			for (Size i = 0; i < presentModeCount; i++)
			{
				if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
				{
					swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
					break;
				}
				if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
				{
					swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
				}
			}
		}

		VkSurfaceCapabilitiesKHR surfCaps;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.instance.physicalDevice, mPresentSurface, &surfCaps);

		// find the transformation of the surface
		VkSurfaceTransformFlagsKHR preTransform;
		if (surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		{
			// We prefer a non-rotated transform
			preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		}
		else
		{
			preTransform = surfCaps.currentTransform;
		}

		VkExtent2D swapchainExtent = {};
		// if width (and height) equals the special value 0xFFFFFFFF, the size of the surface will be set by the swapchain
		if (surfCaps.currentExtent.width == (UInt32)-1)
		{
			// if the surface size is undefined, the size is set to
			// the size of the images requested.
			swapchainExtent.width = width;
			swapchainExtent.height = height;
		}
		else
		{
			// If the surface size is defined, the swap chain size must match
			swapchainExtent = surfCaps.currentExtent;
			width = surfCaps.currentExtent.width;
			height = surfCaps.currentExtent.height;
		}

		imageCount = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
			imageCount = capabilities.maxImageCount;

		VkSwapchainCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = mPresentSurface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = mFormat;
		createInfo.imageColorSpace = mColorSpace;
		createInfo.imageExtent = swapchainExtent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = nullptr;

		createInfo.preTransform = capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = swapchainPresentMode;
		createInfo.clipped = VK_TRUE;

		createInfo.oldSwapchain = oldSwapchain;

		if (vkCreateSwapchainKHR(context.device.logicalDevice, &createInfo, nullptr, &swapchain) != VK_SUCCESS)
		{
			SG_LOG_ERROR("Failed to create swapchain");
			return false;
		}

		// clean up old resources
		if (oldSwapchain != VK_NULL_HANDLE)
		{
			for (auto& e : imageViews)
				vkDestroyImageView(context.device.logicalDevice, e, nullptr);
			vkDestroySwapchainKHR(context.device.logicalDevice, oldSwapchain, nullptr);
			for (UInt32 i = 0; i < imageCount; ++i)
				Delete(mpRts[i]);
		}

		vkGetSwapchainImagesKHR(context.device.logicalDevice, swapchain, &imageCount, nullptr);
		images.resize(imageCount);
		vkGetSwapchainImagesKHR(context.device.logicalDevice, swapchain, &imageCount, images.data());

		imageViews.resize(imageCount);
		mpRts.resize(imageCount);
		for (UInt32 i = 0; i < imageCount; ++i)
		{
			VkImageViewCreateInfo colorAttachmentView = {};
			colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			colorAttachmentView.pNext = NULL;
			colorAttachmentView.format = mFormat;
			colorAttachmentView.components = {
				VK_COMPONENT_SWIZZLE_R,
				VK_COMPONENT_SWIZZLE_G,
				VK_COMPONENT_SWIZZLE_B,
				VK_COMPONENT_SWIZZLE_A
			};
			colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			colorAttachmentView.subresourceRange.baseMipLevel = 0;
			colorAttachmentView.subresourceRange.levelCount = 1;
			colorAttachmentView.subresourceRange.baseArrayLayer = 0;
			colorAttachmentView.subresourceRange.layerCount = 1;
			colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			colorAttachmentView.flags = 0;

			colorAttachmentView.image = images[i];

			vkCreateImageView(context.device.logicalDevice, &colorAttachmentView, nullptr, &imageViews[i]);

			mpRts[i] = New(VulkanRenderTarget, context);
			mpRts[i]->width  = swapchainExtent.width;
			mpRts[i]->height = swapchainExtent.height;
			mpRts[i]->depth  = 1;
			mpRts[i]->array  = 1;
			mpRts[i]->mipLevel = 1;

			mpRts[i]->currLayouts.resize(1);
			mpRts[i]->currLayouts[0] = VK_IMAGE_LAYOUT_UNDEFINED;

			mpRts[i]->format = ToSGImageFormat(mFormat);
			mpRts[i]->sample = ToSGSampleCount(VK_SAMPLE_COUNT_1_BIT);
			mpRts[i]->type   = ToSGImageType(VK_IMAGE_TYPE_2D);
			mpRts[i]->usage  = ToSGImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

			mpRts[i]->image     = images[i];
			mpRts[i]->imageView = imageViews[i];
#if SG_USE_VULKAN_MEMORY_ALLOCATOR
			mpRts[i]->vmaAllocation = nullptr;
#else
			mpRts[i]->memory    = 0;
#endif
			mpRts[i]->mbIsDepth = false;

			mpRts[i]->id = VulkanTexture::msIdAllocator.Allocate();
		}

		return true;
	}

	void VulkanSwapchain::DestroySurface()
	{
		if (mPresentSurface != VK_NULL_HANDLE)
			vkDestroySurfaceKHR(context.instance.instance, mPresentSurface, nullptr);
	}

	VulkanRenderTarget* VulkanSwapchain::GetRenderTarget(UInt32 index) const
	{
		SG_ASSERT(index >= 0 && index < imageCount);
		if (mpRts.empty())
			SG_LOG_WARN("Swapchain not created yet!");
		return mpRts[index];
	}

	void VulkanSwapchain::CleanUp()
	{
		for (UInt32 i = 0; i < imageCount; ++i)
			vkDestroyImageView(context.device.logicalDevice, imageViews[i], nullptr);
		for (auto* ptr : mpRts)
			Delete(ptr);
		if (swapchain != VK_NULL_HANDLE)
			vkDestroySwapchainKHR(context.device.logicalDevice, swapchain, nullptr);
	}

	bool VulkanSwapchain::AcquireNextImage(VulkanSemaphore* pSignalSemaphore, UInt32& imageIndex)
	{
		if (vkAcquireNextImageKHR(context.device.logicalDevice, swapchain, UINT64_MAX, pSignalSemaphore->semaphore, VK_NULL_HANDLE, &imageIndex) != VK_SUCCESS)
		{
			SG_LOG_WARN("Failed to acquire next image!");
			return false;
		}
		return true;
	}

	EImageState VulkanSwapchain::Present(VulkanQueue* queue, UInt32 imageIndex, VulkanSemaphore* pWaitSemaphore)
	{
		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pNext = nullptr;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;
		presentInfo.pImageIndices = &imageIndex;
		if (pWaitSemaphore != nullptr)
		{
			presentInfo.pWaitSemaphores = &pWaitSemaphore->semaphore;
			presentInfo.waitSemaphoreCount = 1;
		}

		VkResult res = vkQueuePresentKHR(queue->handle, &presentInfo);
		if (res == VK_SUCCESS)
			return EImageState::eComplete;
		else if (res == VK_SUBOPTIMAL_KHR)
			return EImageState::eIncomplete;
		return EImageState::eFailure;
	}

	bool VulkanSwapchain::CreateSurface()
	{
#ifdef SG_PLATFORM_WINDOWS
		Window* mainWindow = OperatingSystem::GetMainWindow();

		VkWin32SurfaceCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		createInfo.hwnd = (HWND)mainWindow->GetNativeHandle();
		createInfo.hinstance = ::GetModuleHandle(NULL);
		
		if (vkCreateWin32SurfaceKHR(context.instance.instance, &createInfo, nullptr, &mPresentSurface) != VK_SUCCESS)
			return false;

		CheckSurfacePresentable(context.device.queueFamilyIndices.graphics);

		// Get list of supported surface formats
		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(context.instance.physicalDevice, mPresentSurface, &formatCount, NULL);
		vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(context.instance.physicalDevice, mPresentSurface, &formatCount, surfaceFormats.data());

		// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
		// there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
		if ((formatCount == 1) && (surfaceFormats[0].format == VK_FORMAT_UNDEFINED))
		{
			mFormat = VK_FORMAT_B8G8R8A8_UNORM;
			mColorSpace = surfaceFormats[0].colorSpace;
		}
		else
		{
			// iterate over the list of available surface format and
			// check for the presence of VK_FORMAT_B8G8R8A8_UNORM
			bool bFoundB8G8R8A8UNORM = false;
			for (auto& surfaceFormat : surfaceFormats)
			{
				if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
				{
					mFormat = surfaceFormat.format;
					mColorSpace = surfaceFormat.colorSpace;
					bFoundB8G8R8A8UNORM = true;
					break;
				}
			}

			// in case VK_FORMAT_B8G8R8A8_UNORM is not available
			// select the first available color format
			if (!bFoundB8G8R8A8UNORM)
			{
				mFormat = surfaceFormats[0].format;
				mColorSpace = surfaceFormats[0].colorSpace;
			}
		}

		return true;
#endif
	}

	bool VulkanSwapchain::CheckSurfacePresentable(UInt32 familyIndex)
	{
		// check if the graphic queue can do the presentation job
		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(context.instance.physicalDevice, familyIndex, mPresentSurface, &presentSupport);
		if (!presentSupport)
		{
			SG_LOG_ERROR("Current physical device not support surface presentation");
			return false;
		}
		return true;
	}

}