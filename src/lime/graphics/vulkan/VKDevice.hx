package lime.graphics.vulkan;

import haxe.Int64;

#if (!lime_doc_gen || lime_cffi)
import lime._internal.backend.native.NativeCFFI;
#end

@:access(lime.graphics.VulkanRenderContext)
@:access(lime._internal.backend.native.NativeCFFI)
/**
	A logical Vulkan device created from a `VKPhysicalDevice`.
**/
class VKDevice
{
	public var extensions(default, null):Array<String>;
	public var graphicsQueue(default, null):VKQueue;
	public var handle(default, null):Int64;
	public var instance(default, null):VKInstance;
	public var physicalDevice(default, null):VKPhysicalDevice;
	public var queueFamily(default, null):VKQueueFamilyInfo;

	@:allow(lime.graphics.vulkan.VKPhysicalDevice)
	private function new(physicalDevice:VKPhysicalDevice, queueFamily:VKQueueFamilyInfo, extensions:Array<String>, data:Dynamic)
	{
		this.physicalDevice = physicalDevice;
		this.instance = physicalDevice.instance;
		this.queueFamily = queueFamily;
		this.extensions = extensions.copy();
		handle = VK.__makeHandle(data.handle);
		graphicsQueue = new VKQueue(this, VK.__makeHandle(data.queue), data.queueFamilyIndex, 0);
	}

	public function createCommandPool(queueFamily:VKQueueFamilyInfo = null, flags:Int = VK.COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT):VKCommandPool
	{
		if (queueFamily == null)
		{
			queueFamily = this.queueFamily;
		}

		#if (!macro && lime_cffi && lime_vulkan)
		if (isValid() && queueFamily != null)
		{
			var data:Dynamic = NativeCFFI.lime_vk_create_command_pool(instance.context.__windowHandle, instance.handle.high, instance.handle.low,
				handle.high, handle.low, queueFamily.index, flags);
			var commandPoolHandle = VK.__makeHandle(data);
			if (!VK.__isZero(commandPoolHandle))
			{
				return new VKCommandPool(this, queueFamily.index, commandPoolHandle);
			}
		}
		#end

		return null;
	}

	public function createFence(signaled:Bool = false):VKFence
	{
		#if (!macro && lime_cffi && lime_vulkan)
		if (isValid())
		{
			var flags = signaled ? VK.FENCE_CREATE_SIGNALED_BIT : 0;
			var data:Dynamic = NativeCFFI.lime_vk_create_fence(instance.context.__windowHandle, instance.handle.high, instance.handle.low, handle.high,
				handle.low, flags);
			var fenceHandle = VK.__makeHandle(data);
			if (!VK.__isZero(fenceHandle))
			{
				return new VKFence(this, fenceHandle);
			}
		}
		#end

		return null;
	}

	public function createSemaphore():VKSemaphore
	{
		#if (!macro && lime_cffi && lime_vulkan)
		if (isValid())
		{
			var data:Dynamic = NativeCFFI.lime_vk_create_semaphore(instance.context.__windowHandle, instance.handle.high, instance.handle.low,
				handle.high, handle.low);
			var semaphoreHandle = VK.__makeHandle(data);
			if (!VK.__isZero(semaphoreHandle))
			{
				return new VKSemaphore(this, semaphoreHandle);
			}
		}
		#end

		return null;
	}

	/**
		Destroys the logical device. All resources created from the device should
		be destroyed before calling this method.
	**/
	public function dispose():Void
	{
		#if (!macro && lime_cffi && lime_vulkan)
		if (isValid())
		{
			NativeCFFI.lime_vk_destroy_device(instance.context.__windowHandle, instance.handle.high, instance.handle.low, handle.high, handle.low);
			handle = Int64.ofInt(0);
			graphicsQueue = null;
		}
		#end
	}

	public inline function get():Int64
	{
		return handle;
	}

	public inline function isValid():Bool
	{
		return !VK.__isZero(handle);
	}

	/**
		Blocks until the logical device has completed all submitted work.
	**/
	public function waitIdle():Bool
	{
		#if (!macro && lime_cffi && lime_vulkan)
		if (isValid())
		{
			return NativeCFFI.lime_vk_device_wait_idle(instance.context.__windowHandle, instance.handle.high, instance.handle.low, handle.high, handle.low);
		}
		#end

		return false;
	}
}
