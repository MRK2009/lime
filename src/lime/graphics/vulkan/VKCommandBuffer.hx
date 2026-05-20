package lime.graphics.vulkan;

import haxe.Int64;

#if (!lime_doc_gen || lime_cffi)
import lime._internal.backend.native.NativeCFFI;
#end

@:access(lime.graphics.VulkanRenderContext)
@:access(lime._internal.backend.native.NativeCFFI)
/**
	A Vulkan command buffer allocated from a `VKCommandPool`.
**/
class VKCommandBuffer
{
	public var device(get, never):VKDevice;
	public var handle(default, null):Int64;
	public var level(default, null):Int;
	public var pool(default, null):VKCommandPool;

	@:allow(lime.graphics.vulkan.VKCommandPool)
	private function new(pool:VKCommandPool, handle:Int64, level:Int)
	{
		this.pool = pool;
		this.handle = handle;
		this.level = level;
	}

	public function begin(flags:Int = 0):Bool
	{
		#if (!macro && lime_cffi && lime_vulkan)
		if (isValid() && pool != null && pool.device != null && pool.device.isValid())
		{
			return NativeCFFI.lime_vk_begin_command_buffer(pool.device.instance.context.__windowHandle, pool.device.instance.handle.high,
				pool.device.instance.handle.low, pool.device.handle.high, pool.device.handle.low, handle.high, handle.low, flags);
		}
		#end

		return false;
	}

	public function dispose():Void
	{
		if (pool != null)
		{
			pool.free(this);
		}
	}

	public function end():Bool
	{
		#if (!macro && lime_cffi && lime_vulkan)
		if (isValid() && pool != null && pool.device != null && pool.device.isValid())
		{
			return NativeCFFI.lime_vk_end_command_buffer(pool.device.instance.context.__windowHandle, pool.device.instance.handle.high,
				pool.device.instance.handle.low, pool.device.handle.high, pool.device.handle.low, handle.high, handle.low);
		}
		#end

		return false;
	}

	public inline function get():Int64
	{
		return handle;
	}

	public inline function isValid():Bool
	{
		return !VK.__isZero(handle);
	}

	public function reset(flags:Int = 0):Bool
	{
		#if (!macro && lime_cffi && lime_vulkan)
		if (isValid() && pool != null && pool.device != null && pool.device.isValid())
		{
			return NativeCFFI.lime_vk_reset_command_buffer(pool.device.instance.context.__windowHandle, pool.device.instance.handle.high,
				pool.device.instance.handle.low, pool.device.handle.high, pool.device.handle.low, handle.high, handle.low, flags);
		}
		#end

		return false;
	}

	@:allow(lime.graphics.vulkan.VKCommandPool)
	private function __invalidate():Void
	{
		handle = Int64.ofInt(0);
	}

	private inline function get_device():VKDevice
	{
		return (pool != null) ? pool.device : null;
	}
}
