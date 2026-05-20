package lime.graphics.vulkan;

import haxe.Int64;
#if (!lime_doc_gen || lime_cffi)
import lime.system.CFFI;
#end

/**
	Describes a Vulkan physical device discovered through a `VKInstance`.
**/
class VKPhysicalDevice
{
	public var instance(default, null):VKInstance;
	public var handle(default, null):Int64;
	public var name(default, null):String;
	public var apiVersion(default, null):Int;
	public var apiVersionString(default, null):String;
	public var driverVersion(default, null):Int;
	public var vendorID(default, null):Int;
	public var deviceID(default, null):Int;
	public var deviceType(default, null):Int;
	public var deviceTypeName(default, null):String;
	public var isDiscrete(default, null):Bool;
	public var supportsPresent(default, null):Bool;
	public var queueFamilies(default, null):Array<VKQueueFamilyInfo>;

	@:allow(lime.graphics.vulkan.VKInstance)
	private function new(instance:VKInstance, data:Dynamic)
	{
		this.instance = instance;
		handle = VK.__makeHandle(data.handle);
		name = (data.name != null) ? #if (!lime_doc_gen || lime_cffi) CFFI.stringValue(data.name) #else cast data.name #end : "";
		apiVersion = data.apiVersion;
		apiVersionString = VK.versionString(apiVersion);
		driverVersion = data.driverVersion;
		vendorID = data.vendorID;
		deviceID = data.deviceID;
		deviceType = data.deviceType;
		deviceTypeName = switch (deviceType)
		{
			case 1: "integrated-gpu";
			case 2: "discrete-gpu";
			case 3: "virtual-gpu";
			case 4: "cpu";
			default: "other";
		}
		isDiscrete = (deviceType == 2);
		supportsPresent = false;

		queueFamilies = [];
		var rawQueueFamilies:Dynamic = data.queueFamilies;
		if (rawQueueFamilies != null)
		{
			var length:Int = untyped rawQueueFamilies.length;
			for (i in 0...length)
			{
				var queueFamily = new VKQueueFamilyInfo(untyped rawQueueFamilies[i]);
				queueFamilies.push(queueFamily);
				if (queueFamily.supportsPresent)
				{
					supportsPresent = true;
				}
			}
		}
	}

	/**
		Returns the first queue family that matches the requested capabilities.
	**/
	public function getQueueFamily(requireGraphics:Bool = true, requirePresent:Bool = false, requireCompute:Bool = false,
		requireTransfer:Bool = false):VKQueueFamilyInfo
	{
		for (queueFamily in queueFamilies)
		{
			if (queueFamily.matches(requireGraphics, requirePresent, requireCompute, requireTransfer))
			{
				return queueFamily;
			}
		}

		return null;
	}

	public inline function hasQueueFamily(requireGraphics:Bool = true, requirePresent:Bool = false, requireCompute:Bool = false,
		requireTransfer:Bool = false):Bool
	{
		return getQueueFamily(requireGraphics, requirePresent, requireCompute, requireTransfer) != null;
	}
}
