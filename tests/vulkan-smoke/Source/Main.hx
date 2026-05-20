package;

import haxe.Int64;
import haxe.Timer;
import lime.app.Application;
import lime.graphics.RenderContext;
import lime.graphics.RenderContextType;
import lime.graphics.VulkanRenderContext;
import lime.graphics.vulkan.VK;
import lime.graphics.vulkan.VKRenderer;
import lime.system.System;

class Main extends Application
{
	private var renderer:VKRenderer;
	private var finished:Bool;

	public function new()
	{
		super();
	}

	override public function onWindowCreate():Void
	{
		super.onWindowCreate();

		if (window == null)
		{
			fail("Missing primary window");
			return;
		}

		if (window.context == null || window.context.type != RenderContextType.VULKAN || window.context.vulkan == null)
		{
			fail("Primary window was not created with a Vulkan context");
			return;
		}

		var context = window.context.vulkan;
		validateBootstrap(context);

		renderer = context.createRenderer("LimeVulkanSmoke");
		if (renderer == null || !renderer.isValid)
		{
			fail("Failed to create Vulkan renderer: " + VKRenderer.getLastError());
		}
	}

	override public function render(context:RenderContext):Void
	{
		super.render(context);

		if (finished || renderer == null)
		{
			return;
		}

		if (!renderer.render(0.125, 0.25, 0.5, 1.0))
		{
			fail("Failed to render Vulkan frame: " + renderer.lastError);
			return;
		}

		finished = true;
		Timer.delay(complete, 50);
	}

	private function validateBootstrap(context:VulkanRenderContext):Void
	{
		var extensions = context.getInstanceExtensions();
		if (extensions == null || extensions.length == 0)
		{
			fail("No Vulkan instance extensions were reported");
			return;
		}

		var procAddr = context.getInstanceProcAddr();
		if (!isNonZero(procAddr))
		{
			fail("vkGetInstanceProcAddr was not available");
			return;
		}

		var instance = VK.createInstance(context, "LimeVulkanSmoke");
		if (instance == null || !instance.isValid())
		{
			fail("Failed to create Vulkan instance: " + VK.getLastError());
			return;
		}

		var surface = instance.createSurface();
		if (surface == null || !surface.isValid())
		{
			instance.dispose();
			fail("Failed to create Vulkan surface");
			return;
		}

		var devices = instance.enumeratePhysicalDevices(surface);
		if (devices.length == 0)
		{
			surface.dispose();
			instance.dispose();
			fail("No Vulkan physical devices were enumerated");
			return;
		}

		var device = instance.pickPhysicalDevice(surface);
		if (device == null || !device.hasQueueFamily(true, true))
		{
			surface.dispose();
			instance.dispose();
			fail("No present-capable graphics queue family was found");
			return;
		}

		surface.dispose();
		instance.dispose();
	}

	private function complete():Void
	{
		if (renderer != null)
		{
			renderer.dispose();
			renderer = null;
		}

		if (window != null)
		{
			window.close();
		}
	}

	private function fail(message:String):Void
	{
		trace("Vulkan smoke failed: " + message);

		if (renderer != null)
		{
			renderer.dispose();
			renderer = null;
		}

		System.exit(1);
	}

	private static inline function isNonZero(value:Int64):Bool
	{
		return value.high != 0 || value.low != 0;
	}
}
