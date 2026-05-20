package lime.graphics.vulkan;

import haxe.Int64;
import lime.graphics.VulkanRenderContext;
#if (!lime_doc_gen || lime_cffi)
import lime.system.CFFI;
#end

#if (!lime_doc_gen || lime_cffi)
import lime._internal.backend.native.NativeCFFI;
#end

@:access(lime.graphics.VulkanRenderContext)
@:access(lime._internal.backend.native.NativeCFFI)
/**
	Entry point for Lime's Vulkan API surface.

	Like `lime.graphics.opengl.GL`, this class is the public place for low-level
	graphics API access. Vulkan is explicit by design, so the API is organized
	around handles and lifecycle objects instead of GL-style global state.
**/
class VK
{
	public static inline var SUCCESS = 0;
	public static inline var NOT_READY = 1;
	public static inline var TIMEOUT = 2;
	public static inline var EVENT_SET = 3;
	public static inline var EVENT_RESET = 4;
	public static inline var INCOMPLETE = 5;

	public static inline var KHR_SWAPCHAIN_EXTENSION_NAME = "VK_KHR_swapchain";

	public static inline var PHYSICAL_DEVICE_TYPE_OTHER = 0;
	public static inline var PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1;
	public static inline var PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2;
	public static inline var PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU = 3;
	public static inline var PHYSICAL_DEVICE_TYPE_CPU = 4;

	public static inline var QUEUE_GRAPHICS_BIT = 0x00000001;
	public static inline var QUEUE_COMPUTE_BIT = 0x00000002;
	public static inline var QUEUE_TRANSFER_BIT = 0x00000004;
	public static inline var QUEUE_SPARSE_BINDING_BIT = 0x00000008;

	public static inline var COMMAND_POOL_CREATE_TRANSIENT_BIT = 0x00000001;
	public static inline var COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT = 0x00000002;
	public static inline var COMMAND_POOL_CREATE_PROTECTED_BIT = 0x00000004;

	public static inline var COMMAND_BUFFER_LEVEL_PRIMARY = 0;
	public static inline var COMMAND_BUFFER_LEVEL_SECONDARY = 1;

	public static inline var COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 0x00000001;
	public static inline var COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT = 0x00000002;
	public static inline var COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT = 0x00000004;

	public static inline var COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT = 0x00000001;

	public static inline var FENCE_CREATE_SIGNALED_BIT = 0x00000001;

	/**
		Creates a managed `VkInstance` for the current Lime Vulkan window.
	**/
	public static function createInstance(context:VulkanRenderContext, applicationName:String = "Lime"):VKInstance
	{
		if (context == null)
		{
			return null;
		}

		#if (!macro && lime_cffi)
		var handleData:Dynamic = NativeCFFI.lime_vk_create_instance(context.__windowHandle, applicationName);
		if (handleData != null)
		{
			var handle = __makeHandle(handleData);
			if (!__isZero(handle))
			{
				return new VKInstance(context, handle);
			}
		}
		#end

		return null;
	}

	/**
		Returns the last bootstrap-layer Vulkan error surfaced by Lime.
	**/
	public static function getLastError():String
	{
		#if (!macro && lime_cffi)
		var value:Dynamic = NativeCFFI.lime_vk_get_last_error();
		if (value != null)
		{
			return CFFI.stringValue(value);
		}
		#end

		return "";
	}

	/**
		Picks a preferred physical device from an enumerated device list.
		This currently prefers graphics-capable devices, optionally requiring
		present support, and favors discrete GPUs by default.
	**/
	public static function pickPhysicalDevice(devices:Array<VKPhysicalDevice>, requirePresent:Bool = false,
		preferDiscrete:Bool = true):VKPhysicalDevice
	{
		if (devices == null || devices.length == 0)
		{
			return null;
		}

		var bestDevice:VKPhysicalDevice = null;
		var bestScore = -1;

		for (device in devices)
		{
			var score = __scorePhysicalDevice(device, requirePresent, preferDiscrete);
			if (score > bestScore)
			{
				bestScore = score;
				bestDevice = device;
			}
		}

		return bestDevice;
	}

	public static inline function versionString(version:Int):String
	{
		var major = version >>> 22;
		var minor = (version >>> 12) & 0x3FF;
		var patch = version & 0xFFF;
		return major + "." + minor + "." + patch;
	}

	@:allow(lime.graphics.VulkanRenderContext)
	@:allow(lime.graphics.vulkan.VKCommandBuffer)
	@:allow(lime.graphics.vulkan.VKCommandPool)
	@:allow(lime.graphics.vulkan.VKDevice)
	@:allow(lime.graphics.vulkan.VKFence)
	@:allow(lime.graphics.vulkan.VKInstance)
	@:allow(lime.graphics.vulkan.VKPhysicalDevice)
	@:allow(lime.graphics.vulkan.VKQueue)
	@:allow(lime.graphics.vulkan.VKSemaphore)
	private static function __makeHandle(value:Dynamic):Int64
	{
		if (value == null)
		{
			return Int64.ofInt(0);
		}

		return Int64.make(value.high, value.low);
	}

	@:allow(lime.graphics.VulkanRenderContext)
	@:allow(lime.graphics.vulkan.VKCommandBuffer)
	@:allow(lime.graphics.vulkan.VKCommandPool)
	@:allow(lime.graphics.vulkan.VKDevice)
	@:allow(lime.graphics.vulkan.VKFence)
	@:allow(lime.graphics.vulkan.VKInstance)
	@:allow(lime.graphics.vulkan.VKQueue)
	@:allow(lime.graphics.vulkan.VKSemaphore)
	private static inline function __isZero(handle:Int64):Bool
	{
		return handle.high == 0 && handle.low == 0;
	}

	private static function __scorePhysicalDevice(device:VKPhysicalDevice, requirePresent:Bool, preferDiscrete:Bool):Int
	{
		if (device == null || !device.hasQueueFamily(true, requirePresent))
		{
			return -1;
		}

		var score = 0;

		switch (device.deviceType)
		{
			case PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
				score += preferDiscrete ? 400 : 250;
			case PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
				score += preferDiscrete ? 300 : 400;
			case PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
				score += 150;
			case PHYSICAL_DEVICE_TYPE_CPU:
				score += 100;
			default:
				score += 50;
		}

		var primaryQueue = device.getQueueFamily(true, requirePresent);
		if (primaryQueue != null)
		{
			score += primaryQueue.queueCount * 2;
			if (primaryQueue.supportsTransfer) score += 4;
			if (primaryQueue.supportsCompute) score += 2;
		}

		score += device.queueFamilies.length;
		return score;
	}
}
