package lime.graphics.vulkan;

import haxe.Int64;
import haxe.io.Bytes;

#if (!lime_doc_gen || lime_cffi)
import lime._internal.backend.native.NativeCFFI;
#end

@:access(lime.graphics.VulkanRenderContext)
@:access(lime._internal.backend.native.NativeCFFI)
/**
	Device memory allocated from a `VKDevice`.
**/
class VKDeviceMemory
{
	public var device(default, null):VKDevice;
	public var handle(default, null):Int64;
	public var memoryTypeIndex(default, null):Int;
	public var properties(default, null):Int;
	public var size(default, null):Int64;

	@:allow(lime.graphics.vulkan.VKDevice)
	private function new(device:VKDevice, handle:Int64, size:Int64, memoryTypeIndex:Int, properties:Int)
	{
		this.device = device;
		this.handle = handle;
		this.size = size;
		this.memoryTypeIndex = memoryTypeIndex;
		this.properties = properties;
	}

	public function dispose():Void
	{
		#if (!macro && lime_cffi && lime_vulkan)
		if (isValid() && device != null && device.isValid())
		{
			NativeCFFI.lime_vk_free_memory(device.instance.context.__windowHandle, device.instance.handle.high, device.instance.handle.low,
				device.handle.high, device.handle.low, handle.high, handle.low);
			handle = Int64.ofInt(0);
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
		Copies bytes into host-visible memory. This maps and unmaps internally,
		which is convenient for staging/uniform data and avoids exposing unsafe
		raw pointers at the Lime boundary.
	**/
	public function upload(bytes:Bytes, memoryOffset:Int64 = null, byteOffset:Int = 0, byteLength:Int = -1):Bool
	{
		if (memoryOffset == null)
		{
			memoryOffset = Int64.ofInt(0);
		}
		if (bytes == null)
		{
			return false;
		}
		if (byteLength < 0)
		{
			byteLength = bytes.length - byteOffset;
		}

		#if (!macro && lime_cffi && lime_vulkan)
		if (isValid() && device != null && device.isValid())
		{
			return NativeCFFI.lime_vk_upload_memory(device.instance.context.__windowHandle, device.instance.handle.high, device.instance.handle.low,
				device.handle.high, device.handle.low, handle.high, handle.low, memoryOffset.high, memoryOffset.low, bytes, byteOffset, byteLength);
		}
		#end

		return false;
	}
}
