package lime.graphics.vulkan;

import haxe.Int64;
import lime.graphics.VulkanRenderContext;
import lime.system.CFFI;

#if (!lime_doc_gen || lime_cffi)
import lime._internal.backend.native.NativeCFFI;
#end

@:access(lime.graphics.VulkanRenderContext)
@:access(lime._internal.backend.native.NativeCFFI)
/**
	Entry point for Lime's lightweight Vulkan bootstrap helpers.

	This API is intentionally smaller than `lime.graphics.opengl.GL`. It focuses
	on the native window/bootstrap work needed to get a Vulkan app started while
	Lime's wider Vulkan surface continues to grow.
**/
class VK
{
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
	@:allow(lime.graphics.vulkan.VKInstance)
	@:allow(lime.graphics.vulkan.VKPhysicalDevice)
	private static function __makeHandle(value:Dynamic):Int64
	{
		if (value == null)
		{
			return Int64.ofInt(0);
		}

		return Int64.make(value.high, value.low);
	}

	@:allow(lime.graphics.VulkanRenderContext)
	@:allow(lime.graphics.vulkan.VKInstance)
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
			case 2:
				score += preferDiscrete ? 400 : 250;
			case 1:
				score += preferDiscrete ? 300 : 400;
			case 3:
				score += 150;
			case 4:
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
