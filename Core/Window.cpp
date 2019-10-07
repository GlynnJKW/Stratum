#include <Core/Window.hpp>
#include <Core/Device.hpp>
#include <Util/Profiler.hpp>
#include <Util/Util.hpp>

using namespace std;

void Window::WindowPosCallback(GLFWwindow* window, int x, int y) {
	Window* win = (Window*)glfwGetWindowUserPointer(window);
	glfwGetWindowPos(window, &win->mClientRect.offset.x, &win->mClientRect.offset.y);
	glfwGetWindowSize(window, (int*)&win->mClientRect.extent.width, (int*)&win->mClientRect.extent.height);
}
void Window::FramebufferResizeCallback(GLFWwindow* window, int width, int height) {
	Window* win = (Window*)glfwGetWindowUserPointer(window);
	if (width > 0 && height > 0) {
		glfwGetWindowPos(window, &win->mClientRect.offset.x, &win->mClientRect.offset.y);
		glfwGetWindowSize(window, (int*)&win->mClientRect.extent.width, (int*)&win->mClientRect.extent.height);
		if (win->mDevice) win->CreateSwapchain(win->mDevice);
	}
}
void Window::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (action == GLFW_REPEAT) return;
	Window* win = (Window*)glfwGetWindowUserPointer(window);

	win->mInput->mCurrent.mKeys[key] = action;
	if (win && win->mInput->mCurrent.mKeys[GLFW_KEY_LEFT_ALT] == GLFW_PRESS && win->mInput->mCurrent.mKeys[GLFW_KEY_ENTER] == GLFW_PRESS && (key == GLFW_KEY_ENTER || key == GLFW_KEY_LEFT_ALT))
		win->Fullscreen(!win->Fullscreen());
}
void Window::CursorPosCallback(GLFWwindow* window, double x, double y) {
	Window* win = (Window*)glfwGetWindowUserPointer(window);
	win->mInput->mCurrent.mCursorPos.x = (float)x + win->ClientRect().offset.x;
	win->mInput->mCurrent.mCursorPos.y = (float)y + win->ClientRect().offset.y;
}
void Window::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	Window* win = (Window*)glfwGetWindowUserPointer(window);
	win->mInput->mCurrent.mMouseButtons[button] = action;
}
void Window::ScrollCallback(GLFWwindow* window, double x, double y) {
	Window* win = (Window*)glfwGetWindowUserPointer(window);
	win->mInput->mCurrent.mScrollDelta.x += (float)x;
	win->mInput->mCurrent.mScrollDelta.y += (float)y;
}

Window::Window(VkInstance instance, const string& title, MouseKeyboardInput* input, VkRect2D position, int monitorIndex)
	: mInstance(instance), mDevice(nullptr), mTitle(title), mSwapchainSize({}), mFullscreen(false), mClientRect(position), mWindowedRect({}), mInput(input),
	mSwapchain(VK_NULL_HANDLE), mImageCount(0), mFormat({}),
	mCurrentBackBufferIndex(0), mImageAvailableSemaphoreIndex(0), mFrameData(nullptr) {

	if (position.extent.width == 0 || position.extent.height == 0) {
		position.extent.width = 1600;
		position.extent.height = 900;
	}

	mWindow = glfwCreateWindow(position.extent.width, position.extent.height, mTitle.c_str(), nullptr, nullptr);
	if (mWindow == nullptr) {
		const char* msg;
		glfwGetError(&msg);
		printf("Failed to create GLFW window! %s\n", msg);
		throw runtime_error("Failed to create GLFW window!");
	}

	glfwSetWindowPos(mWindow, position.offset.x, position.offset.y);

	glfwSetWindowUserPointer(mWindow, this);

	if (glfwRawMouseMotionSupported() == GLFW_TRUE)
		glfwSetInputMode(mWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

	glfwSetFramebufferSizeCallback(mWindow, FramebufferResizeCallback);
	glfwSetWindowPosCallback(mWindow, WindowPosCallback);
	glfwSetKeyCallback(mWindow, KeyCallback);
	glfwSetCursorPosCallback(mWindow, CursorPosCallback);
	glfwSetScrollCallback(mWindow, ScrollCallback);
	glfwSetMouseButtonCallback(mWindow, MouseButtonCallback);

	if (glfwCreateWindowSurface(instance, mWindow, nullptr, &mSurface) != VK_SUCCESS) {
		const char* msg;
		glfwGetError(&msg);
		printf("Failed to create GLFW window surface! %s\n", msg);
		throw runtime_error("Failed to create GLFW window surface!");
	}

	glfwGetWindowPos(mWindow, &mClientRect.offset.x, &mClientRect.offset.y);
	glfwGetWindowSize(mWindow, (int*)&mClientRect.extent.width, (int*)&mClientRect.extent.height);
	mWindowedRect = mClientRect;
	
	int monitorCount;
	GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
	if (monitorIndex > -1 && monitorIndex < monitorCount) {
		auto monitor = monitors[monitorIndex];
		auto mode = glfwGetVideoMode(monitor);

		int mx, my;
		glfwGetMonitorPos(monitor, &mx, &my);

		glfwSetWindowAttrib(mWindow, GLFW_DECORATED, GLFW_FALSE);
		glfwSetWindowAttrib(mWindow, GLFW_MAXIMIZED, GLFW_TRUE);
		glfwSetWindowPos(mWindow, mx, my);
		glfwSetWindowSize(mWindow, mode->width, mode->height);

		mFullscreen = true;
	}
}
Window::~Window() {
	DestroySwapchain();
	vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
	glfwDestroyWindow(mWindow);
}

VkImage Window::AcquireNextImage() {
	if (mSwapchain == VK_NULL_HANDLE) return VK_NULL_HANDLE;
	mImageAvailableSemaphoreIndex = (mImageAvailableSemaphoreIndex + 1) % (uint32_t)mImageAvailableSemaphores.size();
	VkResult err = vkAcquireNextImageKHR(*mDevice, mSwapchain, numeric_limits<uint64_t>::max(), mImageAvailableSemaphores[mImageAvailableSemaphoreIndex], VK_NULL_HANDLE, &mCurrentBackBufferIndex);
	if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
		CreateSwapchain(mDevice);

	PROFILER_BEGIN("Wait for GPU");
	for (const auto& f : mFrameData[mCurrentBackBufferIndex].mFences)
		f->Wait();
	PROFILER_END;
	mFrameData[mCurrentBackBufferIndex].mFences.clear();

	if (mSwapchain == VK_NULL_HANDLE) return VK_NULL_HANDLE; // swapchain was destroyed during CreateSwapchain (happens when window is minimized)
	return mFrameData[mCurrentBackBufferIndex].mSwapchainImage;
}

void Window::Present(const vector<shared_ptr<Fence>>& fences) {
	if (mSwapchain == VK_NULL_HANDLE) return;
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &mImageAvailableSemaphores[mImageAvailableSemaphoreIndex];
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &mSwapchain;
	presentInfo.pImageIndices = &mCurrentBackBufferIndex;
	vkQueuePresentKHR(mDevice->PresentQueue(), &presentInfo);
	mFrameData[mCurrentBackBufferIndex].mFences = fences;
}

GLFWmonitor* Window::GetCurrentMonitor(int& x, int& y, int& w, int& h) const {
	int nmonitors, i;
	int wx, wy, ww, wh;
	int mx, my, mw, mh;
	int overlap, bestoverlap;
	GLFWmonitor* bestmonitor;
	GLFWmonitor** monitors;
	const GLFWvidmode* mode;

	bestoverlap = 0;
	bestmonitor = NULL;

	glfwGetWindowPos(mWindow, &wx, &wy);
	glfwGetWindowSize(mWindow, &ww, &wh);
	monitors = glfwGetMonitors(&nmonitors);

	for (i = 0; i < nmonitors; i++) {
		mode = glfwGetVideoMode(monitors[i]);
		glfwGetMonitorPos(monitors[i], &mx, &my);
		mw = mode->width;
		mh = mode->height;

		overlap =
			gmax(0, gmin(wx + ww, mx + mw) - gmax(wx, mx)) *
			gmax(0, gmin(wy + wh, my + mh) - gmax(wy, my));

		if (bestoverlap < overlap) {
			bestoverlap = overlap;
			bestmonitor = monitors[i];
			x = mx;
			y = my;
			w = mw;
			h = mh;
		}
	}

	return bestmonitor;
}

void Window::Fullscreen(bool fs) {
	if (fs) {
		mWindowedRect = mClientRect;

		int x, y, w, h;
		GetCurrentMonitor(x, y, w, h);

		glfwSetWindowAttrib(mWindow, GLFW_DECORATED, GLFW_FALSE);
		glfwSetWindowAttrib(mWindow, GLFW_MAXIMIZED, GLFW_TRUE);

		glfwSetWindowPos(mWindow, x, y);
		glfwSetWindowSize(mWindow, w, h);
		mFullscreen = true;
	} else {
		glfwSetWindowAttrib(mWindow, GLFW_DECORATED, GLFW_TRUE);
		glfwSetWindowAttrib(mWindow, GLFW_MAXIMIZED, GLFW_FALSE);

		glfwSetWindowPos(mWindow, mWindowedRect.offset.x, mWindowedRect.offset.y);
		glfwSetWindowSize(mWindow, mWindowedRect.extent.width, mWindowedRect.extent.height);

		mFullscreen = false;
	}
}

void Window::CreateSwapchain(::Device* device) {
	if (mSwapchain) DestroySwapchain();
	mDevice = device;
	mDevice->SetObjectName(mSurface, mTitle + " Surface");

	#pragma region create swapchain
	SwapChainSupportDetails swapChainSupport;
	QuerySwapChainSupport(device->PhysicalDevice(), mSurface, swapChainSupport);

	// find the size of the swapchain
	if (swapChainSupport.mCapabilities.currentExtent.width != numeric_limits<uint32_t>::max() && swapChainSupport.mCapabilities.currentExtent.width != 0)
		mSwapchainSize = swapChainSupport.mCapabilities.currentExtent;
	else {
		mSwapchainSize.width = std::clamp(mClientRect.extent.width,  swapChainSupport.mCapabilities.minImageExtent.width, swapChainSupport.mCapabilities.maxImageExtent.width);
		mSwapchainSize.height = std::clamp(mClientRect.extent.height, swapChainSupport.mCapabilities.minImageExtent.height, swapChainSupport.mCapabilities.maxImageExtent.height);
	}
	
	if (mSwapchainSize.width == numeric_limits<uint32_t>::max() || mSwapchainSize.height == numeric_limits<uint32_t>::max() ||
		mSwapchainSize.width == 0 || mSwapchainSize.height == 0)
		return;

	// find a preferrable surface format 
	mFormat = swapChainSupport.mFormats[0];
	for (const auto& availableFormat : swapChainSupport.mFormats)
		if (availableFormat.format == VK_FORMAT_R8G8B8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			mFormat = availableFormat;
			break;
		}

	// find the best present mode
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	for (const auto& availablePresentMode : swapChainSupport.mPresentModes)
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			presentMode = availablePresentMode;
			break;
		} else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
			presentMode = availablePresentMode;

	// find the preferrable number of back buffers
	mImageCount = swapChainSupport.mCapabilities.minImageCount + 1;
	if (swapChainSupport.mCapabilities.maxImageCount > 0 && mImageCount > swapChainSupport.mCapabilities.maxImageCount)
		mImageCount = swapChainSupport.mCapabilities.maxImageCount;
	
	VkSwapchainCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = mSurface;
	createInfo.minImageCount = mImageCount;
	createInfo.imageFormat = mFormat.format;
	createInfo.imageColorSpace = mFormat.colorSpace;
	createInfo.imageExtent = mSwapchainSize;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	uint32_t graphicsFamily, presentFamily;
	if (!FindQueueFamilies(mDevice->PhysicalDevice(), mSurface, graphicsFamily, presentFamily)) throw runtime_error("Failed to find queue families");
	uint32_t queueFamilyIndices[] = { graphicsFamily, presentFamily };
	if (graphicsFamily != presentFamily) {
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	} else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0; // Optional
		createInfo.pQueueFamilyIndices = nullptr; // Optional
	}
	createInfo.preTransform = swapChainSupport.mCapabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;
	ThrowIfFailed(vkCreateSwapchainKHR(*mDevice, &createInfo, nullptr, &mSwapchain));
	mDevice->SetObjectName(mSwapchain, mTitle + " Swapchain");
	#pragma endregion

	// get the back buffers
	vector<VkImage> images;
	vkGetSwapchainImagesKHR(*mDevice, mSwapchain, &mImageCount, nullptr);
	images.resize(mImageCount);
	vkGetSwapchainImagesKHR(*mDevice, mSwapchain, &mImageCount, images.data());

	mFrameData = new FrameData[mImageCount];

	mImageAvailableSemaphores.resize(mImageCount);
	mImageAvailableSemaphoreIndex = 0;

	auto commandBuffer = device->GetCommandBuffer();

	// create per-frame objects
	for (uint32_t i = 0; i < mImageCount; i++) {
		mFrameData[i] = FrameData();
		mFrameData[i].mSwapchainImage = images[i];
		mDevice->SetObjectName(images[i], mTitle + " Image " + to_string(i));


		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = mFrameData[i].mSwapchainImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = 0;
		vkCmdPipelineBarrier(*commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		VkImageViewCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = mFrameData[i].mSwapchainImage;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = mFormat.format;
		createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;
		ThrowIfFailed(vkCreateImageView(*mDevice, &createInfo, nullptr, &mFrameData[i].mSwapchainImageView));
		mDevice->SetObjectName(mFrameData[i].mSwapchainImageView, mTitle + " Image View " + to_string(i));

		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		ThrowIfFailed(vkCreateSemaphore(*mDevice, &semaphoreInfo, nullptr, &mImageAvailableSemaphores[i]));
		mDevice->SetObjectName(mImageAvailableSemaphores[i], mTitle + " Image Available Semaphore " + to_string(i));
	}

	device->Execute(commandBuffer)->Wait();
}

void Window::DestroySwapchain() {
	mDevice->FlushCommandBuffers();
	for (uint32_t i = 0; i < mImageCount; i++) {
		for (const auto& f : mFrameData[i].mFences)
			f->Wait();
		mFrameData[i].mFences.clear();
		if (mFrameData[i].mSwapchainImageView != VK_NULL_HANDLE)
			vkDestroyImageView(*mDevice, mFrameData[i].mSwapchainImageView, nullptr);
		if (mImageAvailableSemaphores[i] != VK_NULL_HANDLE)
			vkDestroySemaphore(*mDevice, mImageAvailableSemaphores[i], nullptr);

		mFrameData[i].mSwapchainImageView = VK_NULL_HANDLE;
		mImageAvailableSemaphores[i] = VK_NULL_HANDLE;
	}

	if (mSwapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(*mDevice, mSwapchain, nullptr);
	safe_delete_array(mFrameData);
	mSwapchain = VK_NULL_HANDLE;
}