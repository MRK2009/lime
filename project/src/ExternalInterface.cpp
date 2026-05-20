#ifndef STATIC_LINK
#define IMPLEMENT_API
#endif

#if defined(HX_WINDOWS) || defined(HX_MACOS) || defined(HX_LINUX)
#define NEKO_COMPATIBLE
#endif

#include <system/CFFI.h>

#include <app/Application.h>
#include <app/ApplicationEvent.h>
#include <graphics/format/JPEG.h>
#include <graphics/format/PNG.h>
#include <graphics/utils/ImageDataUtil.h>
#include <graphics/Image.h>
#include <graphics/ImageBuffer.h>
#include <graphics/RenderEvent.h>
#ifdef LIME_VULKAN
#include <graphics/vulkan/VKRenderer.h>
#endif
#include <media/containers/OGG.h>
#include <media/containers/WAV.h>
#include <media/AudioBuffer.h>
#include <system/CFFIPointer.h>
#include <system/Clipboard.h>
#include <system/ClipboardEvent.h>
#include <system/Endian.h>
#include <system/FileWatcher.h>
#include <system/JNI.h>
#include <system/Locale.h>
#include <system/OrientationEvent.h>
#include <system/SensorEvent.h>
#include <system/System.h>
#include <text/Font.h>
#include <ui/Cursor.h>
#include <ui/DropEvent.h>
#include <ui/FileDialog.h>
#include <ui/Gamepad.h>
#include <ui/GamepadEvent.h>
#include <ui/Haptic.h>
#include <ui/Joystick.h>
#include <ui/JoystickEvent.h>
#include <ui/KeyCode.h>
#include <ui/KeyEvent.h>
#include <ui/MouseEvent.h>
#include <ui/TextEvent.h>
#include <ui/TouchEvent.h>
#include <ui/Window.h>
#include <ui/WindowEvent.h>
#include <utils/compress/LZMA.h>
#include <utils/compress/Zlib.h>
#include <vm/NekoVM.h>

#ifdef HX_WINDOWS
#include <locale>
#include <codecvt>
#endif
#include <memory>

#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef LIME_SDL_SOUND
#include <media/SDLSound.h>
#endif

DEFINE_KIND (k_finalizer);


namespace lime {

	static std::string lastVulkanRendererError;
	static std::string lastVKError;


	static uint64_t CombineVulkanHandle (int high, int low) {

		return ((uint64_t)(uint32_t)high << 32) | (uint32_t)low;

	}


	static value CreateVulkanHandleValue (uint64_t handleValue) {

		value result = alloc_empty_object ();
		alloc_field (result, val_id ("low"), alloc_int ((int32_t)(handleValue & 0xFFFFFFFFULL)));
		alloc_field (result, val_id ("high"), alloc_int ((int32_t)(handleValue >> 32)));
		return result;

	}


	static vdynamic* HLCreateVulkanHandleValue (uint64_t handleValue) {

		const int id_low = hl_hash_utf8 ("low");
		const int id_high = hl_hash_utf8 ("high");
		vdynamic* result = (vdynamic*)hl_alloc_dynobj ();
		hl_dyn_seti (result, id_low, &hlt_i32, (int32_t)(handleValue & 0xFFFFFFFFULL));
		hl_dyn_seti (result, id_high, &hlt_i32, (int32_t)(handleValue >> 32));
		return result;

	}


#ifdef LIME_VULKAN
	static VkSurfaceKHR UInt64ToVulkanSurface (uint64_t value) {

		return (VkSurfaceKHR)value;

	}


	static bool CreateManagedVulkanInstance (Window* targetWindow, const char* applicationName, VkInstance* outInstance) {

		if (!targetWindow || !outInstance) {

			lastVKError = "Missing Vulkan window or output instance";
			return false;

		}

		*outInstance = VK_NULL_HANDLE;

		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr ();
		if (!vkGetInstanceProcAddr) {

			lastVKError = "SDL did not expose vkGetInstanceProcAddr";
			return false;

		}

		PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr (VK_NULL_HANDLE, "vkCreateInstance");
		PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties =
			(PFN_vkEnumerateInstanceExtensionProperties)vkGetInstanceProcAddr (VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
		PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion =
			(PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr (VK_NULL_HANDLE, "vkEnumerateInstanceVersion");

		if (!vkCreateInstance || !vkEnumerateInstanceExtensionProperties) {

			lastVKError = "Missing required Vulkan global functions";
			return false;

		}

		unsigned int requiredExtensionCount = 0;
		if (!targetWindow->GetVulkanInstanceExtensions (&requiredExtensionCount, 0)) {

			lastVKError = "Failed to query required Vulkan window extensions";
			return false;

		}

		std::vector<const char*> requiredExtensions (requiredExtensionCount);
		if (requiredExtensionCount > 0 && !targetWindow->GetVulkanInstanceExtensions (&requiredExtensionCount, requiredExtensions.data ())) {

			lastVKError = "Failed to fetch required Vulkan window extensions";
			return false;

		}

		std::vector<const char*> instanceExtensions;
		for (size_t i = 0; i < requiredExtensions.size (); ++i) {

			instanceExtensions.push_back (requiredExtensions[i]);

		}

		bool portabilityEnumerationSupported = false;
		uint32_t availableExtensionCount = 0;
		if (vkEnumerateInstanceExtensionProperties (0, &availableExtensionCount, 0) == VK_SUCCESS && availableExtensionCount > 0) {

			std::vector<VkExtensionProperties> availableExtensions (availableExtensionCount);
			if (vkEnumerateInstanceExtensionProperties (0, &availableExtensionCount, availableExtensions.data ()) == VK_SUCCESS) {

				for (size_t i = 0; i < availableExtensions.size (); ++i) {

					#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
					if (strcmp (availableExtensions[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {

						portabilityEnumerationSupported = true;

					}
					#endif

				}

			}

		}

		if (portabilityEnumerationSupported) {

			#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
			instanceExtensions.push_back (VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			#endif

		}

		uint32_t instanceVersion = VK_API_VERSION_1_0;
		if (vkEnumerateInstanceVersion) {

			vkEnumerateInstanceVersion (&instanceVersion);

		}

		VkApplicationInfo applicationInfo;
		memset (&applicationInfo, 0, sizeof (applicationInfo));
		applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		applicationInfo.pApplicationName = applicationName ? applicationName : "Lime";
		applicationInfo.pEngineName = "Lime";
		applicationInfo.applicationVersion = VK_MAKE_VERSION (1, 0, 0);
		applicationInfo.engineVersion = VK_MAKE_VERSION (1, 0, 0);
		applicationInfo.apiVersion = instanceVersion >= VK_API_VERSION_1_0 ? VK_API_VERSION_1_0 : instanceVersion;

		VkInstanceCreateInfo createInfo;
		memset (&createInfo, 0, sizeof (createInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &applicationInfo;
		createInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size ();
		createInfo.ppEnabledExtensionNames = instanceExtensions.empty () ? 0 : instanceExtensions.data ();

		#ifdef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
		if (portabilityEnumerationSupported) {

			createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

		}
		#endif

		VkResult result = vkCreateInstance (&createInfo, 0, outInstance);
		if (result != VK_SUCCESS) {

			lastVKError = "vkCreateInstance failed";
			return false;

		}

		lastVKError.clear ();
		return true;

	}


	static std::vector<std::string> GetVulkanDeviceExtensions (value extensions) {

		std::vector<std::string> deviceExtensions;

		if (val_is_null (extensions)) {

			deviceExtensions.push_back (VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			return deviceExtensions;

		}

		int length = val_array_size (extensions);
		for (int i = 0; i < length; ++i) {

			value extension = val_array_i (extensions, i);
			if (!val_is_null (extension)) {

				deviceExtensions.push_back (val_string (extension));

			}

		}

		return deviceExtensions;

	}


	static std::vector<std::string> GetHLVulkanDeviceExtensions (hl_varray* extensions) {

		std::vector<std::string> deviceExtensions;

		if (!extensions) {

			deviceExtensions.push_back (VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			return deviceExtensions;

		}

		int length = extensions->size;
		hl_vstring** extensionData = hl_aptr (extensions, hl_vstring*);

		for (int i = 0; i < length; ++i) {

			hl_vstring* extension = *extensionData++;
			if (extension) {

				deviceExtensions.push_back (hl_to_utf8 ((const uchar*)extension->bytes));

			}

		}

		return deviceExtensions;

	}


	static PFN_vkVoidFunction GetManagedVulkanDeviceProc (Window* targetWindow, VkInstance instance, VkDevice device, const char* name) {

		if (!targetWindow || !instance || !device) {

			lastVKError = "Missing Vulkan window, instance, or device";
			return 0;

		}

		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr ();
		if (!vkGetInstanceProcAddr) {

			lastVKError = "SDL did not expose vkGetInstanceProcAddr";
			return 0;

		}

		PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr (instance, "vkGetDeviceProcAddr");
		if (!vkGetDeviceProcAddr) {

			lastVKError = "Missing Vulkan device proc address function";
			return 0;

		}

		PFN_vkVoidFunction proc = vkGetDeviceProcAddr (device, name);
		if (!proc) {

			lastVKError = std::string ("Missing Vulkan device function: ") + name;
			return 0;

		}

		return proc;

	}


	static bool CreateManagedVulkanDevice (Window* targetWindow, VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
		const std::vector<std::string>& requestedExtensions, VkDevice* outDevice, VkQueue* outQueue) {

		if (!targetWindow || !instance || !physicalDevice || !outDevice || !outQueue) {

			lastVKError = "Missing Vulkan instance, physical device, or output device";
			return false;

		}

		*outDevice = VK_NULL_HANDLE;
		*outQueue = VK_NULL_HANDLE;

		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr ();
		if (!vkGetInstanceProcAddr) {

			lastVKError = "SDL did not expose vkGetInstanceProcAddr";
			return false;

		}

		PFN_vkCreateDevice vkCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr (instance, "vkCreateDevice");
		PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr (instance, "vkGetDeviceProcAddr");
		PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties =
			(PFN_vkEnumerateDeviceExtensionProperties)vkGetInstanceProcAddr (instance, "vkEnumerateDeviceExtensionProperties");
		PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties =
			(PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr (instance, "vkGetPhysicalDeviceQueueFamilyProperties");

		if (!vkCreateDevice || !vkGetDeviceProcAddr || !vkEnumerateDeviceExtensionProperties || !vkGetPhysicalDeviceQueueFamilyProperties) {

			lastVKError = "Missing required Vulkan device creation functions";
			return false;

		}

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties (physicalDevice, &queueFamilyCount, 0);
		if (queueFamilyIndex >= queueFamilyCount) {

			lastVKError = "Invalid Vulkan queue family index";
			return false;

		}

		float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueCreateInfo;
		memset (&queueCreateInfo, 0, sizeof (queueCreateInfo));
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		std::vector<const char*> deviceExtensions;
		for (size_t i = 0; i < requestedExtensions.size (); ++i) {

			deviceExtensions.push_back (requestedExtensions[i].c_str ());

		}

		if (!requestedExtensions.empty ()) {

			uint32_t extensionCount = 0;
			VkResult extensionResult = vkEnumerateDeviceExtensionProperties (physicalDevice, 0, &extensionCount, 0);
			if (extensionResult != VK_SUCCESS) {

				lastVKError = "Could not enumerate Vulkan device extensions";
				return false;

			}

			std::vector<VkExtensionProperties> availableExtensions (extensionCount);
			if (extensionCount > 0) {

				extensionResult = vkEnumerateDeviceExtensionProperties (physicalDevice, 0, &extensionCount, availableExtensions.data ());
				if (extensionResult != VK_SUCCESS) {

					lastVKError = "Could not enumerate Vulkan device extensions";
					return false;

				}

			}

			for (size_t i = 0; i < requestedExtensions.size (); ++i) {

				bool extensionSupported = false;

				for (size_t e = 0; e < availableExtensions.size (); ++e) {

					if (strcmp (availableExtensions[e].extensionName, requestedExtensions[i].c_str ()) == 0) {

						extensionSupported = true;
						break;

					}

				}

				if (!extensionSupported) {

					lastVKError = std::string ("Vulkan physical device does not support ") + requestedExtensions[i];
					return false;

				}

			}

		}

		VkPhysicalDeviceFeatures deviceFeatures;
		memset (&deviceFeatures, 0, sizeof (deviceFeatures));

		VkDeviceCreateInfo createInfo;
		memset (&createInfo, 0, sizeof (createInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size ();
		createInfo.ppEnabledExtensionNames = deviceExtensions.empty () ? 0 : deviceExtensions.data ();

		VkResult result = vkCreateDevice (physicalDevice, &createInfo, 0, outDevice);
		if (result != VK_SUCCESS) {

			lastVKError = "vkCreateDevice failed";
			return false;

		}

		PFN_vkGetDeviceQueue vkGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetDeviceProcAddr (*outDevice, "vkGetDeviceQueue");
		if (!vkGetDeviceQueue) {

			PFN_vkDestroyDevice vkDestroyDevice = (PFN_vkDestroyDevice)vkGetDeviceProcAddr (*outDevice, "vkDestroyDevice");
			if (vkDestroyDevice) vkDestroyDevice (*outDevice, 0);
			*outDevice = VK_NULL_HANDLE;
			lastVKError = "Missing Vulkan device queue function";
			return false;

		}

		vkGetDeviceQueue (*outDevice, queueFamilyIndex, 0, outQueue);
		if (!*outQueue) {

			PFN_vkDestroyDevice vkDestroyDevice = (PFN_vkDestroyDevice)vkGetDeviceProcAddr (*outDevice, "vkDestroyDevice");
			if (vkDestroyDevice) vkDestroyDevice (*outDevice, 0);
			*outDevice = VK_NULL_HANDLE;
			lastVKError = "Failed to get Vulkan device queue";
			return false;

		}

		lastVKError.clear ();
		return true;

	}
#endif


	void gc_application (value handle) {

		Application* application = (Application*)val_data (handle);
		delete application;

	}


	void hl_gc_application (HL_CFFIPointer* handle) {

		Application* application = (Application*)handle->ptr;
		delete application;

	}


	void gc_file_watcher (value handle) {

		#ifdef LIME_EFSW
		FileWatcher* watcher = (FileWatcher*)val_data (handle);
		delete watcher;
		#endif

	}


	void hl_gc_file_watcher (HL_CFFIPointer* handle) {

		#ifdef LIME_EFSW
		FileWatcher* watcher = (FileWatcher*)handle->ptr;
		delete watcher;
		#endif

	}


	void gc_font (value handle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (handle);
		delete font;
		#endif

	}


	void hl_gc_font (HL_CFFIPointer* handle) {

		#ifdef LIME_FREETYPE
		Font* font = (Font*)handle->ptr;
		delete font;
		#endif

	}


	void gc_window (value handle) {

		Window* window = (Window*)val_data (handle);
		delete window;

	}


	void hl_gc_window (HL_CFFIPointer* handle) {

		Window* window = (Window*)handle->ptr;
		delete window;

	}


	std::string wstring_utf8 (const std::wstring& val) {

		std::string out;
		unsigned int codepoint = 0;

		for (const wchar_t chr : val) {

			if (chr >= 0xd800 && chr <= 0xdbff) {

				codepoint = ((chr - 0xd800) << 10) + 0x10000;

			} else {

				if (chr >= 0xdc00 && chr <= 0xdfff) {

					codepoint |= chr - 0xdc00;

				} else {

					codepoint = chr;

				}

				if (codepoint <= 0x7f) {

					out.append (1, static_cast<char> (codepoint));

				} else if (codepoint <= 0x7ff) {

					out.append (1, static_cast<char> (0xc0 | ((codepoint >> 6) & 0x1f)));
					out.append (1, static_cast<char> (0x80 | (codepoint & 0x3f)));

				} else if (codepoint <= 0xffff) {

					out.append (1, static_cast<char> (0xe0 | ((codepoint >> 12) & 0x0f)));
					out.append (1, static_cast<char> (0x80 | ((codepoint >> 6) & 0x3f)));
					out.append (1, static_cast<char> (0x80 | (codepoint & 0x3f)));

				} else {

					out.append (1, static_cast<char> (0xf0 | ((codepoint >> 18) & 0x07)));
					out.append (1, static_cast<char> (0x80 | ((codepoint >> 12) & 0x3f)));
					out.append (1, static_cast<char> (0x80 | ((codepoint >> 6) & 0x3f)));
					out.append (1, static_cast<char> (0x80 | (codepoint & 0x3f)));

				}

				codepoint = 0;

			}

		}

		return out;

	}


	vbyte* hl_wstring_to_utf8_bytes (const std::wstring& val) {

		const std::string utf8 (wstring_utf8 (val));
		vbyte* const bytes = hl_alloc_bytes (utf8.size () + 1);
		std::memcpy(bytes, utf8.c_str (), utf8.size () + 1);
		return bytes;

	}


	std::wstring* hxstring_to_wstring (HxString val) {

		if (val.c_str ()) {

			#ifdef HX_WINDOWS
			return new std::wstring (hxs_wchar (val, nullptr));
			#else
			const std::string _val (hxs_utf8 (val, nullptr));
			return new std::wstring (_val.begin (), _val.end ());
			#endif

		} else {

			return 0;

		}

	}


	std::wstring* hxstring_to_wstring (hl_vstring* val) {

		if (val) {

			std::string _val = std::string (hl_to_utf8 (val->bytes));
			#ifdef HX_WINDOWS
			std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			return new std::wstring (converter.from_bytes (_val));
			#else
			return new std::wstring (_val.begin (), _val.end ());
			#endif

		} else {

			return 0;

		}

	}


	value wstring_to_value (std::wstring* val) {

		if (val) {

			#ifdef HX_WINDOWS
			return alloc_wstring (val->c_str ());
			#else
			std::string _val = std::string (val->begin (), val->end ());
			return alloc_string (_val.c_str ());
			#endif

		} else {

			return 0;

		}

	}


	value lime_application_create () {

		Application* application = CreateApplication ();
		return CFFIPointer (application, gc_application);

	}


	HL_PRIM HL_CFFIPointer* HL_NAME(hl_application_create) () {

		Application* application = CreateApplication ();
		return HLCFFIPointer (application, (hl_finalizer)hl_gc_application);

	}


	void lime_application_event_manager_register (value callback, value eventObject) {

		ApplicationEvent::callback = new ValuePointer (callback);
		ApplicationEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_application_event_manager_register) (vclosure* callback, ApplicationEvent* eventObject) {

		ApplicationEvent::callback = new ValuePointer (callback);
		ApplicationEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	int lime_application_exec (value application) {

		Application* app = (Application*)val_data (application);
		return app->Exec ();

	}


	HL_PRIM int HL_NAME(hl_application_exec) (HL_CFFIPointer* application) {

		Application* app = (Application*)application->ptr;
		return app->Exec ();

	}


	void lime_application_init (value application) {

		Application* app = (Application*)val_data (application);
		app->Init ();

	}


	HL_PRIM void HL_NAME(hl_application_init) (HL_CFFIPointer* application) {

		Application* app = (Application*)application->ptr;
		app->Init ();

	}


	int lime_application_quit (value application) {

		Application* app = (Application*)val_data (application);
		return app->Quit ();

	}


	HL_PRIM int HL_NAME(hl_application_quit) (HL_CFFIPointer* application) {

		Application* app = (Application*)application->ptr;
		return app->Quit ();

	}


	void lime_application_set_frame_rate (value application, double frameRate) {

		Application* app = (Application*)val_data (application);
		app->SetFrameRate (frameRate);

	}


	HL_PRIM void HL_NAME(hl_application_set_frame_rate) (HL_CFFIPointer* application, double frameRate) {

		Application* app = (Application*)application->ptr;
		app->SetFrameRate (frameRate);

	}


	void lime_application_set_main_loop (value application, int profile, double frameRate, int timePrecision, int busyWait, int uncapMode) {

		Application* app = (Application*)val_data (application);
		app->SetMainLoop (profile, frameRate, timePrecision, busyWait, uncapMode);

	}


	HL_PRIM void HL_NAME(hl_application_set_main_loop) (HL_CFFIPointer* application, int profile, double frameRate, int timePrecision, int busyWait, int uncapMode) {

		Application* app = (Application*)application->ptr;
		app->SetMainLoop (profile, frameRate, timePrecision, busyWait, uncapMode);

	}


	void lime_application_set_vsync_mode (value application, int vsyncMode) {

		Application* app = (Application*)val_data (application);
		app->SetVSyncMode (vsyncMode);

	}


	HL_PRIM void HL_NAME(hl_application_set_vsync_mode) (HL_CFFIPointer* application, int vsyncMode) {

		Application* app = (Application*)application->ptr;
		app->SetVSyncMode (vsyncMode);

	}


	bool lime_application_update (value application) {

		Application* app = (Application*)val_data (application);
		return app->Update ();

	}


	HL_PRIM bool HL_NAME(hl_application_update) (HL_CFFIPointer* application) {

		Application* app = (Application*)application->ptr;
		return app->Update ();

	}


	value lime_audio_load_bytes (value data, value buffer) {

		Resource resource;
		Bytes bytes;

		AudioBuffer audioBuffer = AudioBuffer (buffer);

		bytes.Set (data);
		resource = Resource (&bytes);

		#ifdef LIME_SDL_SOUND
		if (SDLSound::Decode (&resource, &audioBuffer)) {

			return audioBuffer.Value (buffer);

		}
		#endif


		if (WAV::Decode (&resource, &audioBuffer)) {

			return audioBuffer.Value (buffer);

		}

		#ifdef LIME_OGG
		if (OGG::Decode (&resource, &audioBuffer)) {

			return audioBuffer.Value (buffer);

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM AudioBuffer* HL_NAME(hl_audio_load_bytes) (Bytes* data, AudioBuffer* buffer) {

		Resource resource = Resource (data);

		#ifdef LIME_SDL_SOUND
		if (SDLSound::Decode (&resource, buffer)) {

			return buffer;

		}
		#endif

		if (WAV::Decode (&resource, buffer)) {

			return buffer;

		}

		#ifdef LIME_OGG
		if (OGG::Decode (&resource, buffer)) {

			return buffer;

		}
		#endif

		return 0;

	}


	value lime_audio_load_file (value data, value buffer) {

		Resource resource;

		AudioBuffer audioBuffer = AudioBuffer (buffer);

		resource = Resource (val_string (data));

		#ifdef LIME_SDL_SOUND
		if (SDLSound::Decode (&resource, &audioBuffer)) {

			return audioBuffer.Value (buffer);

		}
		#endif

		if (WAV::Decode (&resource, &audioBuffer)) {

			return audioBuffer.Value (buffer);

		}

		#ifdef LIME_OGG
		if (OGG::Decode (&resource, &audioBuffer)) {

			return audioBuffer.Value (buffer);

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM AudioBuffer* HL_NAME(hl_audio_load_file) (hl_vstring* data, AudioBuffer* buffer) {

		Resource resource = Resource (data ? hl_to_utf8 ((const uchar*)data->bytes) : NULL);

		#ifdef LIME_SDL_SOUND
		if (SDLSound::Decode (&resource, buffer)) {

			return buffer;

		}
		#endif

		if (WAV::Decode (&resource, buffer)) {

			return buffer;

		}

		#ifdef LIME_OGG
		if (OGG::Decode (&resource, buffer)) {

			return buffer;

		}
		#endif

		return 0;

	}


	value lime_audio_load (value data, value buffer) {

		if (val_is_string (data)) {

			return lime_audio_load_file (data, buffer);

		} else {

			return lime_audio_load_bytes (data, buffer);

		}

	}


	value lime_bytes_from_data_pointer (double data, int length, value _bytes) {

		uintptr_t ptr = (uintptr_t)data;
		Bytes bytes (_bytes);
		bytes.Resize (length);

		if (ptr) {

			memcpy (bytes.b, (const void*)ptr, length);

		}

		return bytes.Value (_bytes);

	}


	HL_PRIM Bytes* HL_NAME(hl_bytes_from_data_pointer) (double data, int length, Bytes* bytes) {

		uintptr_t ptr = (uintptr_t)data;
		bytes->Resize (length);

		if (ptr) {

			memcpy (bytes->b, (const void*)ptr, length);

		}

		return bytes;

	}


	double lime_bytes_get_data_pointer (value bytes) {

		Bytes data = Bytes (bytes);
		return (uintptr_t)data.b;

	}


	HL_PRIM double HL_NAME(hl_bytes_get_data_pointer) (Bytes* bytes) {

		return bytes ? (uintptr_t)bytes->b : 0;

	}


	double lime_bytes_get_data_pointer_offset (value bytes, int offset) {

		if (val_is_null (bytes)) return 0;

		Bytes data = Bytes (bytes);
		return (uintptr_t)data.b + offset;

	}


	HL_PRIM double HL_NAME(hl_bytes_get_data_pointer_offset) (Bytes* bytes, int offset) {

		if (!bytes) return 0;
		return (uintptr_t)bytes->b + offset;

	}


	value lime_bytes_read_file (HxString path, value bytes) {

		Bytes data (bytes);
		data.ReadFile (hxs_utf8 (path, nullptr));
		return data.Value (bytes);

	}


	HL_PRIM Bytes* HL_NAME(hl_bytes_read_file) (hl_vstring* path, Bytes* bytes) {

		if (!path) return 0;
		bytes->ReadFile (hl_to_utf8 ((const uchar*)path->bytes));
		return bytes;

	}


	double lime_cffi_get_native_pointer (value handle) {

		return (uintptr_t)val_data (handle);

	}


	HL_PRIM double HL_NAME(hl_cffi_get_native_pointer) (HL_CFFIPointer* handle) {

		return (uintptr_t)handle->ptr;

	}


	void lime_cffi_finalizer (value abstract) {

		val_call0 ((value)val_data (abstract));

	}


	value lime_cffi_set_finalizer (value callback) {

		value abstract = alloc_abstract (k_finalizer, callback);
		val_gc (abstract, lime_cffi_finalizer);
		return abstract;

	}


	void lime_clipboard_event_manager_register (value callback, value eventObject) {

		ClipboardEvent::callback = new ValuePointer (callback);
		ClipboardEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_clipboard_event_manager_register) (vclosure* callback, ClipboardEvent* eventObject) {

		ClipboardEvent::callback = new ValuePointer (callback);
		ClipboardEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	value lime_clipboard_get_text () {

		if (Clipboard::HasText ()) {

			const char* text = Clipboard::GetText ();
			value _text = alloc_string (text);

			// TODO: Should we free for all backends? (SDL requires it)

			free ((char*)text);
			return _text;

		} else {

			return alloc_null ();

		}

	}


	HL_PRIM vbyte* HL_NAME(hl_clipboard_get_text) () {

		if (Clipboard::HasText ()) {

			const char* text = Clipboard::GetText ();
			return (vbyte*)text;

		} else {

			return 0;

		}

	}


	void lime_clipboard_set_text (HxString text) {

		Clipboard::SetText (hxs_utf8 (text, nullptr));

	}


	HL_PRIM void HL_NAME(hl_clipboard_set_text) (hl_vstring* text) {

		Clipboard::SetText (text ? (const char*)hl_to_utf8 ((const uchar*)text->bytes) : NULL);

	}


	double lime_data_pointer_offset (double pointer, int offset) {

		return (uintptr_t)pointer + offset;

	}


	HL_PRIM double HL_NAME(hl_data_pointer_offset) (double pointer, int offset) {

		return (uintptr_t)pointer + offset;

	}


	value lime_deflate_compress (value buffer, value bytes) {

		#ifdef LIME_ZLIB
		Bytes data (buffer);
		Bytes result (bytes);

		Zlib::Compress (DEFLATE, &data, &result);

		return result.Value (bytes);
		#else
		return alloc_null();
		#endif

	}


	HL_PRIM Bytes* HL_NAME(hl_deflate_compress) (Bytes* buffer, Bytes* bytes) {

		#ifdef LIME_ZLIB
		Zlib::Compress (DEFLATE, buffer, bytes);
		return bytes;
		#else
		return 0;
		#endif

	}


	value lime_deflate_decompress (value buffer, value bytes) {

		#ifdef LIME_ZLIB
		Bytes data (buffer);
		Bytes result (bytes);

		Zlib::Decompress (DEFLATE, &data, &result);

		return result.Value (bytes);
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM Bytes* HL_NAME(hl_deflate_decompress) (Bytes* buffer, Bytes* bytes) {

		#ifdef LIME_ZLIB
		Zlib::Decompress (DEFLATE, buffer, bytes);
		return bytes;
		#else
		return 0;
		#endif

	}


	void lime_drop_event_manager_register (value callback, value eventObject) {

		DropEvent::callback = new ValuePointer (callback);
		DropEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_drop_event_manager_register) (vclosure* callback, DropEvent* eventObject) {

		DropEvent::callback = new ValuePointer (callback);
		DropEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	value lime_file_dialog_open_directory (HxString title, HxString filter, HxString defaultPath) {

		#ifdef LIME_TINYFILEDIALOGS

		std::wstring* _title = hxstring_to_wstring (title);
		std::wstring* _filter = hxstring_to_wstring (filter);
		std::wstring* _defaultPath = hxstring_to_wstring (defaultPath);

		std::wstring* path = FileDialog::OpenDirectory (_title, _filter, _defaultPath);

		if (_title) delete _title;
		if (_filter) delete _filter;
		if (_defaultPath) delete _defaultPath;

		if (path) {

			value _path = wstring_to_value (path);
			delete path;
			return _path;

		} else {

			return alloc_null ();

		}

		#endif

		return alloc_null ();

	}


	HL_PRIM vbyte* HL_NAME(hl_file_dialog_open_directory) (hl_vstring* title, hl_vstring* filter, hl_vstring* defaultPath) {

		#ifdef LIME_TINYFILEDIALOGS

		std::wstring* _title = hxstring_to_wstring (title);
		std::wstring* _filter = hxstring_to_wstring (filter);
		std::wstring* _defaultPath = hxstring_to_wstring (defaultPath);

		std::wstring* path = FileDialog::OpenDirectory (_title, _filter, _defaultPath);

		if (_title) delete _title;
		if (_filter) delete _filter;
		if (_defaultPath) delete _defaultPath;

		if (path) {

			vbyte* const result = hl_wstring_to_utf8_bytes (*path);
			delete path;
			return result;

		} else {

			return NULL;

		}

		#endif

		return NULL;

	}


	value lime_file_dialog_open_file (HxString title, HxString filter, HxString defaultPath) {

		#ifdef LIME_TINYFILEDIALOGS

		std::wstring* _title = hxstring_to_wstring (title);
		std::wstring* _filter = hxstring_to_wstring (filter);
		std::wstring* _defaultPath = hxstring_to_wstring (defaultPath);

		std::wstring* path = FileDialog::OpenFile (_title, _filter, _defaultPath);

		if (_title) delete _title;
		if (_filter) delete _filter;
		if (_defaultPath) delete _defaultPath;

		if (path) {

			value _path = wstring_to_value (path);
			delete path;
			return _path;

		} else {

			return alloc_null ();

		}

		#endif

		return alloc_null ();

	}


	HL_PRIM vbyte* HL_NAME(hl_file_dialog_open_file) (hl_vstring* title, hl_vstring* filter, hl_vstring* defaultPath) {

		#ifdef LIME_TINYFILEDIALOGS

		std::wstring* _title = hxstring_to_wstring (title);
		std::wstring* _filter = hxstring_to_wstring (filter);
		std::wstring* _defaultPath = hxstring_to_wstring (defaultPath);

		std::wstring* path = FileDialog::OpenFile (_title, _filter, _defaultPath);

		if (_title) delete _title;
		if (_filter) delete _filter;
		if (_defaultPath) delete _defaultPath;

		if (path) {

			vbyte* const result = hl_wstring_to_utf8_bytes (*path);
			delete path;
			return result;

		} else {

			return NULL;

		}

		#endif

		return NULL;

	}


	value lime_file_dialog_open_files (HxString title, HxString filter, HxString defaultPath) {

		#ifdef LIME_TINYFILEDIALOGS

		std::wstring* _title = hxstring_to_wstring (title);
		std::wstring* _filter = hxstring_to_wstring (filter);
		std::wstring* _defaultPath = hxstring_to_wstring (defaultPath);

		std::vector<std::wstring*> files;

		FileDialog::OpenFiles (&files, _title, _filter, _defaultPath);
		value result = alloc_array (files.size ());

		if (_title) delete _title;
		if (_filter) delete _filter;
		if (_defaultPath) delete _defaultPath;

		for (int i = 0; i < files.size (); i++) {

			value _file = wstring_to_value (files[i]);
			val_array_set_i (result, i, _file);
			delete files[i];

		}

		#else
		value result = alloc_array (0);
		#endif

		return result;

	}


	HL_PRIM hl_varray* HL_NAME(hl_file_dialog_open_files) (hl_vstring* title, hl_vstring* filter, hl_vstring* defaultPath) {

		#ifdef LIME_TINYFILEDIALOGS

		std::wstring* _title = hxstring_to_wstring (title);
		std::wstring* _filter = hxstring_to_wstring (filter);
		std::wstring* _defaultPath = hxstring_to_wstring (defaultPath);

		std::vector<std::wstring*> files;

		FileDialog::OpenFiles (&files, _title, _filter, _defaultPath);
		hl_varray* result = (hl_varray*)hl_alloc_array (&hlt_bytes, files.size ());
		vbyte** resultData = hl_aptr (result, vbyte*);

		if (_title) delete _title;
		if (_filter) delete _filter;
		if (_defaultPath) delete _defaultPath;

		for (int i = 0; i < files.size (); i++) {

			*resultData++ = hl_wstring_to_utf8_bytes (*files[i]);
			delete files[i];

		}

		#else
		hl_varray* result = hl_alloc_array (&hlt_bytes, 0);
		#endif

		return result;

	}


	value lime_file_dialog_save_file (HxString title, HxString filter, HxString defaultPath) {

		#ifdef LIME_TINYFILEDIALOGS

		std::wstring* _title = hxstring_to_wstring (title);
		std::wstring* _filter = hxstring_to_wstring (filter);
		std::wstring* _defaultPath = hxstring_to_wstring (defaultPath);

		std::wstring* path = FileDialog::SaveFile (_title, _filter, _defaultPath);

		if (_title) delete _title;
		if (_filter) delete _filter;
		if (_defaultPath) delete _defaultPath;

		if (path) {

			value _path = wstring_to_value (path);
			delete path;
			return _path;

		} else {

			return alloc_null ();

		}

		#endif

		return alloc_null ();

	}


	HL_PRIM vbyte* HL_NAME(hl_file_dialog_save_file) (hl_vstring* title, hl_vstring* filter, hl_vstring* defaultPath) {

		#ifdef LIME_TINYFILEDIALOGS

		std::wstring* _title = hxstring_to_wstring (title);
		std::wstring* _filter = hxstring_to_wstring (filter);
		std::wstring* _defaultPath = hxstring_to_wstring (defaultPath);

		std::wstring* path = FileDialog::SaveFile (_title, _filter, _defaultPath);

		if (_title) delete _title;
		if (_filter) delete _filter;
		if (_defaultPath) delete _defaultPath;

		if (path) {

			vbyte* const result = hl_wstring_to_utf8_bytes (*path);
			delete path;
			return result;

		} else {

			return NULL;

		}

		#endif

		return NULL;

	}


	value lime_file_watcher_create (value callback) {

		#ifdef LIME_EFSW
		FileWatcher* watcher = new FileWatcher (callback);
		return CFFIPointer (watcher, gc_file_watcher);
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM HL_CFFIPointer* HL_NAME(hl_file_watcher_create) (vclosure* callback) {

		// #ifdef LIME_EFSW
		// FileWatcher* watcher = new FileWatcher (callback);
		// return HLCFFIPointer (watcher, (hl_finalizer)hl_gc_file_watcher);
		// #else
		return 0;
		// #endif

	}


	value lime_file_watcher_add_directory (value handle, value path, bool recursive) {

		#ifdef LIME_EFSW
		FileWatcher* watcher = (FileWatcher*)val_data (handle);
		return alloc_int (watcher->AddDirectory (val_string (path), recursive));
		#else
		return alloc_int (0);
		#endif

	}


	HL_PRIM int HL_NAME(hl_file_watcher_add_directory) (HL_CFFIPointer* handle, hl_vstring* path, bool recursive) {

		#ifdef LIME_EFSW
		FileWatcher* watcher = (FileWatcher*)handle->ptr;
		return watcher->AddDirectory ((const char*)path, recursive);
		#else
		return 0;
		#endif

	}


	void lime_file_watcher_remove_directory (value handle, value watchID) {

		#ifdef LIME_EFSW
		FileWatcher* watcher = (FileWatcher*)val_data (handle);
		watcher->RemoveDirectory (val_int (watchID));
		#endif

	}


	HL_PRIM void HL_NAME(hl_file_watcher_remove_directory) (HL_CFFIPointer* handle, int watchID) {

		#ifdef LIME_EFSW
		FileWatcher* watcher = (FileWatcher*)handle->ptr;
		watcher->RemoveDirectory (watchID);
		#endif

	}


	void lime_file_watcher_update (value handle) {

		#ifdef LIME_EFSW
		FileWatcher* watcher = (FileWatcher*)val_data (handle);
		watcher->Update ();
		#endif

	}


	HL_PRIM void HL_NAME(hl_file_watcher_update) (HL_CFFIPointer* handle) {

		#ifdef LIME_EFSW
		FileWatcher* watcher = (FileWatcher*)handle->ptr;
		watcher->Update ();
		#endif

	}


	int lime_font_get_ascender (value fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return font->GetAscender ();
		#else
		return 0;
		#endif

	}


	HL_PRIM int HL_NAME(hl_font_get_ascender) (HL_CFFIPointer* fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return font->GetAscender ();
		#else
		return 0;
		#endif

	}


	int lime_font_get_descender (value fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return font->GetDescender ();
		#else
		return 0;
		#endif

	}


	HL_PRIM int HL_NAME(hl_font_get_descender) (HL_CFFIPointer* fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return font->GetDescender ();
		#else
		return 0;
		#endif

	}


	value lime_font_get_family_name (value fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		wchar_t *name = font->GetFamilyName ();
		value result = alloc_wstring (name);
		delete name;
		return result;
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM vbyte* HL_NAME(hl_font_get_family_name) (HL_CFFIPointer* fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		wchar_t *name = font->GetFamilyName ();
		if (!name)
			return nullptr;
		vbyte* const result = hl_wstring_to_utf8_bytes (name);
		delete name;
		return result;
		#else
		return 0;
		#endif

	}


	int lime_font_get_glyph_index (value fontHandle, HxString character) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return font->GetGlyphIndex (hxs_utf8 (character, nullptr));
		#else
		return -1;
		#endif

	}


	HL_PRIM int HL_NAME(hl_font_get_glyph_index) (HL_CFFIPointer* fontHandle, hl_vstring* character) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return font->GetGlyphIndex (character ? (char*)hl_to_utf8 ((const uchar*)character->bytes) : NULL);
		#else
		return -1;
		#endif

	}


	value lime_font_get_glyph_indices (value fontHandle, HxString characters) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return (value)font->GetGlyphIndices (true, hxs_utf8 (characters, nullptr));
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM hl_varray* HL_NAME(hl_font_get_glyph_indices) (HL_CFFIPointer* fontHandle, hl_vstring* characters) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return (hl_varray*)font->GetGlyphIndices (false, characters ? (char*)hl_to_utf8 ((const uchar*)characters->bytes) : NULL);
		#else
		return 0;
		#endif

	}


	value lime_font_get_glyph_metrics (value fontHandle, int index) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return (value)font->GetGlyphMetrics (true, index);
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM vdynamic* HL_NAME(hl_font_get_glyph_metrics) (HL_CFFIPointer* fontHandle, int index) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return (vdynamic*)font->GetGlyphMetrics (false, index);
		#else
		return 0;
		#endif

	}


	int lime_font_get_height (value fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return font->GetHeight ();
		#else
		return 0;
		#endif

	}


	HL_PRIM int HL_NAME(hl_font_get_height) (HL_CFFIPointer* fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return font->GetHeight ();
		#else
		return 0;
		#endif

	}


	int lime_font_get_num_glyphs (value fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return font->GetNumGlyphs ();
		#else
		return 0;
		#endif

	}


	HL_PRIM int HL_NAME(hl_font_get_num_glyphs) (HL_CFFIPointer* fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return font->GetNumGlyphs ();
		#else
		return 0;
		#endif

	}


	int lime_font_get_underline_position (value fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return font->GetUnderlinePosition ();
		#else
		return 0;
		#endif

	}


	HL_PRIM int HL_NAME(hl_font_get_underline_position) (HL_CFFIPointer* fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return font->GetUnderlinePosition ();
		#else
		return 0;
		#endif

	}


	int lime_font_get_underline_thickness (value fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return font->GetUnderlineThickness ();
		#else
		return 0;
		#endif

	}


	HL_PRIM int HL_NAME(hl_font_get_underline_thickness) (HL_CFFIPointer* fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return font->GetUnderlineThickness ();
		#else
		return 0;
		#endif

	}


	int lime_font_get_strikethrough_position (value fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return font->GetStrikethroughPosition ();
		#else
		return 0;
		#endif

	}


	HL_PRIM int HL_NAME(hl_font_get_strikethrough_position) (HL_CFFIPointer* fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return font->GetStrikethroughPosition ();
		#else
		return 0;
		#endif

	}


	int lime_font_get_strikethrough_thickness (value fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return font->GetStrikethroughThickness ();
		#else
		return 0;
		#endif

	}


	HL_PRIM int HL_NAME(hl_font_get_strikethrough_thickness) (HL_CFFIPointer* fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return font->GetStrikethroughThickness ();
		#else
		return 0;
		#endif

	}


	int lime_font_get_units_per_em (value fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return font->GetUnitsPerEM ();
		#else
		return 0;
		#endif

	}


	HL_PRIM int HL_NAME(hl_font_get_units_per_em) (HL_CFFIPointer* fontHandle) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return font->GetUnitsPerEM ();
		#else
		return 0;
		#endif

	}


	value lime_font_load_bytes (value data) {

		#ifdef LIME_FREETYPE
		Resource resource;
		Bytes bytes;

		bytes.Set (data);
		resource = Resource (&bytes);

		Font *font = new Font (&resource, 0);

		if (font) {

			if (font->face) {

				return CFFIPointer (font, gc_font);

			} else {

				delete font;

			}

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM HL_CFFIPointer* HL_NAME(hl_font_load_bytes) (Bytes* data) {

		#ifdef LIME_FREETYPE
		Resource resource = Resource (data);

		Font *font = new Font (&resource, 0);

		if (font) {

			if (font->face) {

				return HLCFFIPointer (font, (hl_finalizer)hl_gc_font);

			} else {

				delete font;

			}

		}
		#endif

		return 0;

	}


	value lime_font_load_file (value data) {

		#ifdef LIME_FREETYPE
		Resource resource = Resource (val_string (data));

		Font *font = new Font (&resource, 0);

		if (font) {

			if (font->face) {

				return CFFIPointer (font, gc_font);

			} else {

				delete font;

			}

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM HL_CFFIPointer* HL_NAME(hl_font_load_file) (hl_vstring* data) {

		#ifdef LIME_FREETYPE
		Resource resource = Resource (data ? hl_to_utf8 ((const uchar*)data->bytes) : NULL);

		Font *font = new Font (&resource, 0);

		if (font) {

			if (font->face) {

				return HLCFFIPointer (font, (hl_finalizer)hl_gc_font);

			} else {

				delete font;

			}

		}
		#endif

		return 0;

	}


	value lime_font_load (value data) {

		if (val_is_string (data)) {

			return lime_font_load_file (data);

		} else {

			return lime_font_load_bytes (data);

		}

	}


	value lime_font_outline_decompose (value fontHandle, int size) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return (value)font->Decompose (true, size);
		#else
		return alloc_null ();
		#endif

	}


	value lime_font_outline_decompose_no_hint (value fontHandle, int size) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		return (value)font->Decompose (true, size, false);
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM vdynamic* HL_NAME(hl_font_outline_decompose) (HL_CFFIPointer* fontHandle, int size) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return (vdynamic*)font->Decompose (false, size);
		#else
		return 0;
		#endif

	}


	HL_PRIM vdynamic* HL_NAME(hl_font_outline_decompose_no_hint) (HL_CFFIPointer* fontHandle, int size) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		return (vdynamic*)font->Decompose (false, size, false);
		#else
		return 0;
		#endif

	}


	value lime_font_render_glyph (value fontHandle, int index, value data) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		Bytes bytes (data);

		if (font->RenderGlyph (index, &bytes)) {

			return bytes.Value (data);

		}
		#endif

		return alloc_null ();

	}


	value lime_font_render_glyph_with_flags (value fontHandle, int index, int loadFlags, value data) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		Bytes bytes (data);

		if (font->RenderGlyphWithFlags (index, loadFlags, &bytes)) {

			return bytes.Value (data);

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM Bytes* HL_NAME(hl_font_render_glyph) (HL_CFFIPointer* fontHandle, int index, Bytes* data) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;

		if (font->RenderGlyph (index, data)) {

			return data;

		}
		#endif

		return NULL;

	}


	HL_PRIM Bytes* HL_NAME(hl_font_render_glyph_with_flags) (HL_CFFIPointer* fontHandle, int index, int loadFlags, Bytes* data) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;

		if (font->RenderGlyphWithFlags (index, loadFlags, data)) {

			return data;

		}
		#endif

		return NULL;

	}


	value lime_font_render_glyphs (value fontHandle, value indices, value data) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		Bytes bytes (data);

		if (font->RenderGlyphs (indices, &bytes)) {

			return bytes.Value (data);

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM Bytes* HL_NAME(hl_font_render_glyphs) (HL_CFFIPointer* fontHandle, hl_varray* indices, Bytes* data) {

		// #ifdef LIME_FREETYPE
		// Font *font = (Font*)fontHandle->ptr;
		// return font->RenderGlyphs (indices, &bytes);
		// #else
		return NULL;
		// #endif

	}


	void lime_font_set_size (value fontHandle, int fontSize, int dpi) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)val_data (fontHandle);
		font->SetSize (fontSize, dpi);
		#endif

	}


	HL_PRIM void HL_NAME(hl_font_set_size) (HL_CFFIPointer* fontHandle, int fontSize, int dpi) {

		#ifdef LIME_FREETYPE
		Font *font = (Font*)fontHandle->ptr;
		font->SetSize (fontSize, dpi);
		#endif

	}


	void lime_gamepad_add_mappings (value mappings) {

		int length = val_array_size (mappings);

		for (int i = 0; i < length; i++) {

			Gamepad::AddMapping (val_string (val_array_i (mappings, i)));

		}

	}


	HL_PRIM void HL_NAME(hl_gamepad_add_mappings) (hl_varray* mappings) {

		int length = mappings->size;
		hl_vstring** mappingsData = hl_aptr (mappings, hl_vstring*);

		for (int i = 0; i < length; i++) {

			Gamepad::AddMapping (hl_to_utf8 ((const uchar*)((*mappingsData++)->bytes)));

		}

	}


	void lime_gamepad_event_manager_register (value callback, value eventObject) {

		GamepadEvent::callback = new ValuePointer (callback);
		GamepadEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_gamepad_event_manager_register) (vclosure* callback, GamepadEvent* eventObject) {

		GamepadEvent::callback = new ValuePointer (callback);
		GamepadEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	value lime_gamepad_get_device_guid (int id) {

		const char* guid = Gamepad::GetDeviceGUID (id);

		if (guid) {

			value result = alloc_string (guid);
			delete guid;
			return result;

		} else {

			return alloc_null ();

		}

	}


	HL_PRIM vbyte* HL_NAME(hl_gamepad_get_device_guid) (int id) {

		const char* guid = Gamepad::GetDeviceGUID (id);

		if (guid) {

			return (vbyte*)guid;

		} else {

			return 0;

		}

	}


	value lime_gamepad_get_device_name (int id) {

		const char* name = Gamepad::GetDeviceName (id);
		return name ? alloc_string (name) : alloc_null ();

	}


	HL_PRIM vbyte* HL_NAME(hl_gamepad_get_device_name) (int id) {

		return (vbyte*)Gamepad::GetDeviceName (id);

	}


	void lime_gamepad_rumble (int id, double lowFrequencyRumble, double highFrequencyRumble, int duration) {

		Gamepad::Rumble (id, lowFrequencyRumble, highFrequencyRumble, duration);

	}


	HL_PRIM void HL_NAME(hl_gamepad_rumble) (int id, double lowFrequencyRumble, double highFrequencyRumble, int duration) {

		Gamepad::Rumble (id, lowFrequencyRumble, highFrequencyRumble, duration);

	}


	value lime_gzip_compress (value buffer, value bytes) {

		#ifdef LIME_ZLIB
		Bytes data (buffer);
		Bytes result (bytes);

		Zlib::Compress (GZIP, &data, &result);

		return result.Value (bytes);
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM Bytes* HL_NAME(hl_gzip_compress) (Bytes* buffer, Bytes* bytes) {

		#ifdef LIME_ZLIB
		Zlib::Compress (GZIP, buffer, bytes);
		return bytes;
		#else
		return 0;
		#endif

	}


	value lime_gzip_decompress (value buffer, value bytes) {

		#ifdef LIME_ZLIB
		Bytes data (buffer);
		Bytes result (bytes);

		Zlib::Decompress (GZIP, &data, &result);

		return result.Value (bytes);
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM Bytes* HL_NAME(hl_gzip_decompress) (Bytes* buffer, Bytes* bytes) {

		#ifdef LIME_ZLIB
		Zlib::Decompress (GZIP, buffer, bytes);
		return bytes;
		#else
		return 0;
		#endif

	}


	void lime_haptic_vibrate (int period, int duration) {

		#ifdef IPHONE
		Haptic::Vibrate (period, duration);
		#endif

	}


	HL_PRIM void HL_NAME(hl_haptic_vibrate) (int period, int duration) {

		#ifdef IPHONE
		Haptic::Vibrate (period, duration);
		#endif

	}


	value lime_image_encode (value buffer, int type, int quality, value bytes) {

		ImageBuffer imageBuffer = ImageBuffer (buffer);
		Bytes data = Bytes (bytes);

		switch (type) {

			case 0:

				#ifdef LIME_PNG
				if (PNG::Encode (&imageBuffer, &data)) {

					return data.Value (bytes);

				}
				#endif
				break;

			case 1:

				#ifdef LIME_JPEG
				if (JPEG::Encode (&imageBuffer, &data, quality)) {

					return data.Value (bytes);

				}
				#endif
				break;

			default: break;

		}

		return alloc_null ();

	}


	HL_PRIM Bytes* HL_NAME(hl_image_encode) (ImageBuffer* buffer, int type, int quality, Bytes* bytes) {

		switch (type) {

			case 0:

				#ifdef LIME_PNG
				if (PNG::Encode (buffer, bytes)) {

					return bytes;

				}
				#endif
				break;

			case 1:

				#ifdef LIME_JPEG
				if (JPEG::Encode (buffer, bytes, quality)) {

					return bytes;

				}
				#endif
				break;

			default: break;

		}

		return 0;

	}


	value lime_image_load_bytes (value data, value buffer) {

		Resource resource;
		Bytes bytes;

		ImageBuffer imageBuffer = ImageBuffer (buffer);

		bytes.Set (data);
		resource = Resource (&bytes);

		#ifdef LIME_PNG
		if (PNG::Decode (&resource, &imageBuffer)) {

			return imageBuffer.Value (buffer);

		}
		#endif

		#ifdef LIME_JPEG
		if (JPEG::Decode (&resource, &imageBuffer)) {

			return imageBuffer.Value (buffer);

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM ImageBuffer* HL_NAME(hl_image_load_bytes) (Bytes* data, ImageBuffer* buffer) {

		Resource resource = Resource (data);

		#ifdef LIME_PNG
		if (PNG::Decode (&resource, buffer)) {

			return buffer;

		}
		#endif

		#ifdef LIME_JPEG
		if (JPEG::Decode (&resource, buffer)) {

			return buffer;

		}
		#endif

		return 0;

	}


	value lime_image_load_file (value data, value buffer) {

		Resource resource = Resource (val_string (data));
		ImageBuffer imageBuffer = ImageBuffer (buffer);

		#ifdef LIME_PNG
		if (PNG::Decode (&resource, &imageBuffer)) {

			return imageBuffer.Value (buffer);

		}
		#endif

		#ifdef LIME_JPEG
		if (JPEG::Decode (&resource, &imageBuffer)) {

			return imageBuffer.Value (buffer);

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM ImageBuffer* HL_NAME(hl_image_load_file) (hl_vstring* data, ImageBuffer* buffer) {

		Resource resource = Resource (data);

		#ifdef LIME_PNG
		if (PNG::Decode (&resource, buffer)) {

			return buffer;

		}
		#endif

		#ifdef LIME_JPEG
		if (JPEG::Decode (&resource, buffer)) {

			return buffer;

		}
		#endif

		return 0;

	}


	value lime_image_load (value data, value buffer) {

		if (val_is_string (data)) {

			return lime_image_load_file (data, buffer);

		} else {

			return lime_image_load_bytes (data, buffer);

		}

	}


	void lime_image_data_util_color_transform (value image, value rect, value colorMatrix) {

		Image _image = Image (image);
		Rectangle _rect = Rectangle (rect);
		ColorMatrix _colorMatrix = ColorMatrix (colorMatrix);
		ImageDataUtil::ColorTransform (&_image, &_rect, &_colorMatrix);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_color_transform) (Image* image, Rectangle* rect, ArrayBufferView* colorMatrix) {

		ColorMatrix _colorMatrix = ColorMatrix (colorMatrix);
		ImageDataUtil::ColorTransform (image, rect, &_colorMatrix);

	}


	void lime_image_data_util_copy_channel (value image, value sourceImage, value sourceRect, value destPoint, int srcChannel, int destChannel) {

		Image _image = Image (image);
		Image _sourceImage = Image (sourceImage);
		Rectangle _sourceRect = Rectangle (sourceRect);
		Vector2 _destPoint = Vector2 (destPoint);
		ImageDataUtil::CopyChannel (&_image, &_sourceImage, &_sourceRect, &_destPoint, srcChannel, destChannel);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_copy_channel) (Image* image, Image* sourceImage, Rectangle* sourceRect, Vector2* destPoint, int srcChannel, int destChannel) {

		ImageDataUtil::CopyChannel (image, sourceImage, sourceRect, destPoint, srcChannel, destChannel);

	}


	void lime_image_data_util_copy_pixels (value image, value sourceImage, value sourceRect, value destPoint, value alphaImage, value alphaPoint, bool mergeAlpha) {

		Image _image = Image (image);
		Image _sourceImage = Image (sourceImage);
		Rectangle _sourceRect = Rectangle (sourceRect);
		Vector2 _destPoint = Vector2 (destPoint);

		if (val_is_null (alphaImage)) {

			ImageDataUtil::CopyPixels (&_image, &_sourceImage, &_sourceRect, &_destPoint, 0, 0, mergeAlpha);

		} else {

			Image _alphaImage = Image (alphaImage);
			Vector2 _alphaPoint = Vector2 (alphaPoint);

			ImageDataUtil::CopyPixels (&_image, &_sourceImage, &_sourceRect, &_destPoint, &_alphaImage, &_alphaPoint, mergeAlpha);

		}

	}


	HL_PRIM void HL_NAME(hl_image_data_util_copy_pixels) (Image* image, Image* sourceImage, Rectangle* sourceRect, Vector2* destPoint, Image* alphaImage, Vector2* alphaPoint, bool mergeAlpha) {

		if (!alphaImage) {

			ImageDataUtil::CopyPixels (image, sourceImage, sourceRect, destPoint, NULL, NULL, mergeAlpha);

		} else {

			if (!alphaPoint) {

				Vector2 _alphaPoint = Vector2 (0, 0);

				ImageDataUtil::CopyPixels (image, sourceImage, sourceRect, destPoint, alphaImage, &_alphaPoint, mergeAlpha);

			} else {

				ImageDataUtil::CopyPixels (image, sourceImage, sourceRect, destPoint, alphaImage, alphaPoint, mergeAlpha);

			}

		}

	}


	void lime_image_data_util_fill_rect (value image, value rect, int rg, int ba) {

		Image _image = Image (image);
		Rectangle _rect = Rectangle (rect);
		int32_t color = (rg << 16) | ba;
		ImageDataUtil::FillRect (&_image, &_rect, color);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_fill_rect) (Image* image, Rectangle* rect, int rg, int ba) {

		int32_t color = (rg << 16) | ba;
		ImageDataUtil::FillRect (image, rect, color);

	}


	void lime_image_data_util_flood_fill (value image, int x, int y, int rg, int ba) {

		Image _image = Image (image);
		int32_t color = (rg << 16) | ba;
		ImageDataUtil::FloodFill (&_image, x, y, color);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_flood_fill) (Image* image, int x, int y, int rg, int ba) {

		int32_t color = (rg << 16) | ba;
		ImageDataUtil::FloodFill (image, x, y, color);

	}


	void lime_image_data_util_get_pixels (value image, value rect, int format, value bytes) {

		Image _image = Image (image);
		Rectangle _rect = Rectangle (rect);
		PixelFormat _format = (PixelFormat)format;
		Bytes pixels = Bytes (bytes);
		ImageDataUtil::GetPixels (&_image, &_rect, _format, &pixels);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_get_pixels) (Image* image, Rectangle* rect, PixelFormat format, Bytes* bytes) {

		ImageDataUtil::GetPixels (image, rect, format, bytes);

	}


	void lime_image_data_util_merge (value image, value sourceImage, value sourceRect, value destPoint, int redMultiplier, int greenMultiplier, int blueMultiplier, int alphaMultiplier) {

		Image _image = Image (image);
		Image _sourceImage = Image (sourceImage);
		Rectangle _sourceRect = Rectangle (sourceRect);
		Vector2 _destPoint = Vector2 (destPoint);
		ImageDataUtil::Merge (&_image, &_sourceImage, &_sourceRect, &_destPoint, redMultiplier, greenMultiplier, blueMultiplier, alphaMultiplier);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_merge) (Image* image, Image* sourceImage, Rectangle* sourceRect, Vector2* destPoint, int redMultiplier, int greenMultiplier, int blueMultiplier, int alphaMultiplier) {

		ImageDataUtil::Merge (image, sourceImage, sourceRect, destPoint, redMultiplier, greenMultiplier, blueMultiplier, alphaMultiplier);

	}


	void lime_image_data_util_multiply_alpha (value image) {

		Image _image = Image (image);
		ImageDataUtil::MultiplyAlpha (&_image);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_multiply_alpha) (Image* image) {

		ImageDataUtil::MultiplyAlpha (image);

	}


	void lime_image_data_util_resize (value image, value buffer, int width, int height) {

		Image _image = Image (image);
		ImageBuffer _buffer = ImageBuffer (buffer);
		ImageDataUtil::Resize (&_image, &_buffer, width, height);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_resize) (Image* image, ImageBuffer* buffer, int width, int height) {

		ImageDataUtil::Resize (image, buffer, width, height);

	}


	void lime_image_data_util_set_format (value image, int format) {

		Image _image = Image (image);
		PixelFormat _format = (PixelFormat)format;
		ImageDataUtil::SetFormat (&_image, _format);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_set_format) (Image* image, PixelFormat format) {

		ImageDataUtil::SetFormat (image, format);

	}


	void lime_image_data_util_set_pixels (value image, value rect, value bytes, int offset, int format, int endian) {

		Image _image = Image (image);
		Rectangle _rect = Rectangle (rect);
		Bytes _bytes (bytes);
		PixelFormat _format = (PixelFormat)format;
		Endian _endian = (Endian)endian;
		ImageDataUtil::SetPixels (&_image, &_rect, &_bytes, offset, _format, _endian);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_set_pixels) (Image* image, Rectangle* rect, Bytes* bytes, int offset, PixelFormat format, Endian endian) {

		ImageDataUtil::SetPixels (image, rect, bytes, offset, format, endian);

	}


	int lime_image_data_util_threshold (value image, value sourceImage, value sourceRect, value destPoint, int operation, int thresholdRG, int thresholdBA, int colorRG, int colorBA, int maskRG, int maskBA, bool copySource) {

		Image _image = Image (image);
		Image _sourceImage = Image (sourceImage);
		Rectangle _sourceRect = Rectangle (sourceRect);
		Vector2 _destPoint = Vector2 (destPoint);
		int32_t threshold = (thresholdRG << 16) | thresholdBA;
		int32_t color = (colorRG << 16) | colorBA;
		int32_t mask = (maskRG << 16) | maskBA;
		return ImageDataUtil::Threshold (&_image, &_sourceImage, &_sourceRect, &_destPoint, operation, threshold, color, mask, copySource);

	}


	HL_PRIM int HL_NAME(hl_image_data_util_threshold) (Image* image, Image* sourceImage, Rectangle* sourceRect, Vector2* destPoint, int operation, int thresholdRG, int thresholdBA, int colorRG, int colorBA, int maskRG, int maskBA, bool copySource) {

		int32_t threshold = (thresholdRG << 16) | thresholdBA;
		int32_t color = (colorRG << 16) | colorBA;
		int32_t mask = (maskRG << 16) | maskBA;
		return ImageDataUtil::Threshold (image, sourceImage, sourceRect, destPoint, operation, threshold, color, mask, copySource);

	}


	void lime_image_data_util_unmultiply_alpha (value image) {

		Image _image = Image (image);
		ImageDataUtil::UnmultiplyAlpha (&_image);

	}


	HL_PRIM void HL_NAME(hl_image_data_util_unmultiply_alpha) (Image* image) {

		ImageDataUtil::UnmultiplyAlpha (image);

	}


	double lime_jni_getenv () {

		#ifdef ANDROID
		return (uintptr_t)JNI::GetEnv ();
		#else
		return 0;
		#endif

	}


	HL_PRIM double HL_NAME(hl_jni_getenv) () {

		#ifdef ANDROID
		return (uintptr_t)JNI::GetEnv ();
		#else
		return 0;
		#endif

	}


	void lime_joystick_event_manager_register (value callback, value eventObject) {

		JoystickEvent::callback = new ValuePointer (callback);
		JoystickEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_joystick_event_manager_register) (vclosure* callback, JoystickEvent* eventObject) {

		JoystickEvent::callback = new ValuePointer (callback);
		JoystickEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	value lime_joystick_get_device_guid (int id) {

		const char* guid = Joystick::GetDeviceGUID (id);
		return guid ? alloc_string (guid) : alloc_null ();

	}


	HL_PRIM vbyte* HL_NAME(hl_joystick_get_device_guid) (int id) {

		return (vbyte*)Joystick::GetDeviceGUID (id);

	}


	value lime_joystick_get_device_name (int id) {

		const char* name = Joystick::GetDeviceName (id);
		return name ? alloc_string (name) : alloc_null ();

	}


	HL_PRIM vbyte* HL_NAME(hl_joystick_get_device_name) (int id) {

		return (vbyte*)Joystick::GetDeviceName (id);

	}


	int lime_joystick_get_num_axes (int id) {

		return Joystick::GetNumAxes (id);

	}


	HL_PRIM int HL_NAME(hl_joystick_get_num_axes) (int id) {

		return Joystick::GetNumAxes (id);

	}


	int lime_joystick_get_num_buttons (int id) {

		return Joystick::GetNumButtons (id);

	}


	HL_PRIM int HL_NAME(hl_joystick_get_num_buttons) (int id) {

		return Joystick::GetNumButtons (id);

	}


	int lime_joystick_get_num_hats (int id) {

		return Joystick::GetNumHats (id);

	}


	HL_PRIM int HL_NAME(hl_joystick_get_num_hats) (int id) {

		return Joystick::GetNumHats (id);

	}


	value lime_jpeg_decode_bytes (value data, bool decodeData, value buffer) {

		ImageBuffer imageBuffer (buffer);

		Bytes bytes (data);
		Resource resource = Resource (&bytes);

		#ifdef LIME_JPEG
		if (JPEG::Decode (&resource, &imageBuffer, decodeData)) {

			return imageBuffer.Value (buffer);

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM ImageBuffer* HL_NAME(hl_jpeg_decode_bytes) (Bytes* data, bool decodeData, ImageBuffer* buffer) {

		Resource resource = Resource (data);

		#ifdef LIME_JPEG
		if (JPEG::Decode (&resource, buffer, decodeData)) {

			return buffer;

		}
		#endif

		return 0;

	}


	value lime_jpeg_decode_file (HxString path, bool decodeData, value buffer) {

		ImageBuffer imageBuffer (buffer);
		Resource resource = Resource (hxs_utf8 (path, nullptr));

		#ifdef LIME_JPEG
		if (JPEG::Decode (&resource, &imageBuffer, decodeData)) {

			return imageBuffer.Value (buffer);

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM ImageBuffer* HL_NAME(hl_jpeg_decode_file) (hl_vstring* path, bool decodeData, ImageBuffer* buffer) {

		Resource resource = Resource (path);

		#ifdef LIME_JPEG
		if (JPEG::Decode (&resource, buffer, decodeData)) {

			return buffer;

		}
		#endif

		return 0;

	}


	int lime_key_code_from_scan_code (int scanCode) {

		return KeyCode::FromScanCode (scanCode);

	}


	HL_PRIM int HL_NAME(hl_key_code_from_scan_code) (int scanCode) {

		return KeyCode::FromScanCode (scanCode);

	}


	int lime_key_code_to_scan_code (int keyCode) {

		return KeyCode::ToScanCode (keyCode);

	}


	HL_PRIM int HL_NAME(hl_key_code_to_scan_code) (int keyCode) {

		return KeyCode::ToScanCode (keyCode);

	}


	void lime_key_event_manager_register (value callback, value eventObject) {

		KeyEvent::callback = new ValuePointer (callback);
		KeyEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_key_event_manager_register) (vclosure* callback, KeyEvent* eventObject) {

		KeyEvent::callback = new ValuePointer (callback);
		KeyEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	value lime_locale_get_system_locale () {

		std::string* locale = Locale::GetSystemLocale ();

		if (!locale) {

			return alloc_null ();

		} else {

			value result = alloc_string (locale->c_str ());
			delete locale;
			return result;

		}

	}


	HL_PRIM vbyte* HL_NAME(hl_locale_get_system_locale) () {

		std::string* locale = Locale::GetSystemLocale ();

		if (!locale) {

			return 0;

		} else {

			int size = locale->size ();
			char* _locale = (char*)malloc (size + 1);
			strncpy (_locale, locale->c_str (), size);
			_locale[size] = '\0';
			delete locale;

			return (vbyte*)_locale;

		}

	}


	value lime_lzma_compress (value buffer, value bytes) {

		#ifdef LIME_LZMA
		Bytes data (buffer);
		Bytes result (bytes);

		LZMA::Compress (&data, &result);

		return result.Value (bytes);
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM Bytes* HL_NAME(hl_lzma_compress) (Bytes* buffer, Bytes* bytes) {

		#ifdef LIME_LZMA
		LZMA::Compress (buffer, bytes);
		return bytes;
		#else
		return 0;
		#endif

	}


	value lime_lzma_decompress (value buffer, value bytes) {

		#ifdef LIME_LZMA
		Bytes data (buffer);
		Bytes result (bytes);

		LZMA::Decompress (&data, &result);

		return result.Value (bytes);
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM Bytes* HL_NAME(hl_lzma_decompress) (Bytes* buffer, Bytes* bytes) {

		#ifdef LIME_LZMA
		LZMA::Decompress (buffer, bytes);
		return bytes;
		#else
		return 0;
		#endif

	}


	void lime_mouse_event_manager_register (value callback, value eventObject) {

		MouseEvent::callback = new ValuePointer (callback);
		MouseEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_mouse_event_manager_register) (vclosure* callback, MouseEvent* eventObject) {

		MouseEvent::callback = new ValuePointer (callback);
		MouseEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	void lime_neko_execute (HxString module) {

		#ifdef LIME_NEKO
		NekoVM::Execute (module.c_str ());
		#endif

	}


	void lime_orientation_event_manager_register (value callback, value eventObject) {

		OrientationEvent::callback = new ValuePointer (callback);
		OrientationEvent::eventObject = new ValuePointer (eventObject);
		System::EnableDeviceOrientationChange(true);

	}


	HL_PRIM void HL_NAME(hl_orientation_event_manager_register) (vclosure* callback, OrientationEvent* eventObject) {

		OrientationEvent::callback = new ValuePointer (callback);
		OrientationEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	value lime_png_decode_bytes (value data, bool decodeData, value buffer) {

		ImageBuffer imageBuffer (buffer);
		Bytes bytes (data);
		Resource resource = Resource (&bytes);

		#ifdef LIME_PNG
		if (PNG::Decode (&resource, &imageBuffer, decodeData)) {

			return imageBuffer.Value (buffer);

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM ImageBuffer* HL_NAME(hl_png_decode_bytes) (Bytes* data, bool decodeData, ImageBuffer* buffer) {

		Resource resource = Resource (data);

		#ifdef LIME_PNG
		if (PNG::Decode (&resource, buffer, decodeData)) {

			return buffer;

		}
		#endif

		return 0;

	}


	value lime_png_decode_file (HxString path, bool decodeData, value buffer) {

		ImageBuffer imageBuffer (buffer);
		Resource resource = Resource (hxs_utf8 (path, nullptr));

		#ifdef LIME_PNG
		if (PNG::Decode (&resource, &imageBuffer, decodeData)) {

			return imageBuffer.Value (buffer);

		}
		#endif

		return alloc_null ();

	}


	HL_PRIM ImageBuffer* HL_NAME(hl_png_decode_file) (hl_vstring* path, bool decodeData, ImageBuffer* buffer) {

		Resource resource = Resource (path);

		#ifdef LIME_PNG
		if (PNG::Decode (&resource, buffer, decodeData)) {

			return buffer;

		}
		#endif

		return 0;

	}


	void lime_render_event_manager_register (value callback, value eventObject) {

		RenderEvent::callback = new ValuePointer (callback);
		RenderEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_render_event_manager_register) (vclosure* callback, RenderEvent* eventObject) {

		RenderEvent::callback = new ValuePointer (callback);
		RenderEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	void lime_sensor_event_manager_register (value callback, value eventObject) {

		SensorEvent::callback = new ValuePointer (callback);
		SensorEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_sensor_event_manager_register) (vclosure* callback, SensorEvent* eventObject) {

		SensorEvent::callback = new ValuePointer (callback);
		SensorEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	bool lime_system_get_allow_screen_timeout () {

		return System::GetAllowScreenTimeout ();

	}


	HL_PRIM bool HL_NAME(hl_system_get_allow_screen_timeout) () {

		return System::GetAllowScreenTimeout ();

	}


	value lime_system_get_device_model () {

		std::wstring* model = System::GetDeviceModel ();

		if (model) {

			value result = alloc_wstring (model->c_str ());
			delete model;
			return result;

		} else {

			return alloc_null ();

		}

	}


	HL_PRIM vbyte* HL_NAME(hl_system_get_device_model) () {

		#ifndef EMSCRIPTEN

		std::wstring* model = System::GetDeviceModel ();

		if (model) {

			vbyte* const result = hl_wstring_to_utf8_bytes (*model);
			delete model;
			return result;

		}

		#endif

		return 0;

	}


	value lime_system_get_device_vendor () {

		std::wstring* vendor = System::GetDeviceVendor ();

		if (vendor) {

			value result = alloc_wstring (vendor->c_str ());
			delete vendor;
			return result;

		} else {

			return alloc_null ();

		}

	}


	HL_PRIM vbyte* HL_NAME(hl_system_get_device_vendor) () {

		#ifndef EMSCRIPTEN

		std::wstring* vendor = System::GetDeviceVendor ();

		if (vendor) {

			vbyte* const result = hl_wstring_to_utf8_bytes (*vendor);
			delete vendor;
			return result;

		}

		#endif

		return 0;

	}


	value lime_system_get_directory (int type, HxString company, HxString title) {

		std::wstring* path = System::GetDirectory ((SystemDirectory)type, hxs_utf8 (company, nullptr), hxs_utf8 (title, nullptr));

		if (path) {

			value result = alloc_wstring (path->c_str ());
			delete path;
			return result;

		} else {

			return alloc_null ();

		}

	}


	HL_PRIM vbyte* HL_NAME(hl_system_get_directory) (int type, hl_vstring* company, hl_vstring* title) {

		#ifndef EMSCRIPTEN

		std::wstring* path = System::GetDirectory ((SystemDirectory)type, company ? (char*)hl_to_utf8 ((const uchar*)company->bytes) : NULL, title ? (char*)hl_to_utf8 ((const uchar*)title->bytes) : NULL);

		if (path) {

			vbyte* const result = hl_wstring_to_utf8_bytes (*path);
			delete path;
			return result;

		}

		#endif

		return 0;

	}


	value lime_system_get_display (int id) {

		return (value)System::GetDisplay (true, id);

	}


	HL_PRIM vdynamic* HL_NAME(hl_system_get_display) (int id) {

		return (vdynamic*)System::GetDisplay (false, id);

	}


	bool lime_system_get_ios_tablet () {

		#ifdef IPHONE
		return System::GetIOSTablet ();
		#else
		return false;
		#endif

	}


	HL_PRIM bool HL_NAME(hl_system_get_ios_tablet) () {

		#ifdef IPHONE
		return System::GetIOSTablet ();
		#else
		return false;
		#endif

	}


	int lime_system_get_num_displays () {

		return System::GetNumDisplays ();

	}


	HL_PRIM int HL_NAME(hl_system_get_num_displays) () {

		return System::GetNumDisplays ();

	}


	int lime_system_get_device_orientation () {

		return System::GetDeviceOrientation();

	}


	HL_PRIM int HL_NAME(hl_system_get_device_orientation) () {

		return System::GetDeviceOrientation();

	}


	value lime_system_get_platform_label () {

		std::wstring* label = System::GetPlatformLabel ();

		if (label) {

			value result = alloc_wstring (label->c_str ());
			delete label;
			return result;

		} else {

			return alloc_null ();

		}

	}


	HL_PRIM vbyte* HL_NAME(hl_system_get_platform_label) () {

		#ifndef EMSCRIPTEN

		std::wstring* label = System::GetPlatformLabel ();

		if (label) {

			vbyte* const result = hl_wstring_to_utf8_bytes (*label);
			delete label;
			return result;

		}

		#endif

		return 0;

	}


	value lime_system_get_platform_name () {

		std::wstring* name = System::GetPlatformName ();

		if (name) {

			value result = alloc_wstring (name->c_str ());
			delete name;
			return result;

		} else {

			return 0;

		}

	}


	HL_PRIM vbyte* HL_NAME(hl_system_get_platform_name) () {

		#ifndef EMSCRIPTEN

		std::wstring* name = System::GetPlatformName ();

		if (name) {

			vbyte* const result = hl_wstring_to_utf8_bytes (*name);
			delete name;
			return result;

		}

		#endif

		return 0;

	}


	value lime_system_get_platform_version () {

		std::wstring* version = System::GetPlatformVersion ();

		if (version) {

			value result = alloc_wstring (version->c_str ());
			delete version;
			return result;

		} else {

			return alloc_null ();

		}

	}


	HL_PRIM vbyte* HL_NAME(hl_system_get_platform_version) () {

		#ifndef EMSCRIPTEN

		std::wstring* version = System::GetPlatformVersion ();

		if (version) {

			vbyte* const result = hl_wstring_to_utf8_bytes (*version);
			delete version;
			return result;
		}

		#endif

		return 0;

	}


	double lime_system_get_timer () {

		return System::GetTimer ();

	}


	HL_PRIM double HL_NAME(hl_system_get_timer) () {

		return System::GetTimer ();

	}


	int lime_system_get_windows_console_mode (int handleType) {

		#if defined (HX_WINDOWS) && !defined (HX_WINRT)
		return System::GetWindowsConsoleMode (handleType);
		#else
		return 0;
		#endif

	}


	HL_PRIM int HL_NAME(hl_system_get_windows_console_mode) (int handleType) {

		#if defined (HX_WINDOWS) && !defined (HX_WINRT)
		return System::GetWindowsConsoleMode (handleType);
		#else
		return 0;
		#endif

	}


	void lime_system_open_file (HxString path) {

		#ifdef IPHONE
		System::OpenFile (path.c_str ());
		#endif

	}


	HL_PRIM void HL_NAME(hl_system_open_file) (vbyte* path) {

		#ifdef IPHONE
		System::OpenFile ((char*)path);
		#endif

	}


	void lime_system_open_url (HxString url, HxString target) {

		#ifdef IPHONE
		System::OpenURL (url.c_str (), target.c_str ());
		#endif

	}


	HL_PRIM void HL_NAME(hl_system_open_url) (vbyte* url, vbyte* target) {

		#ifdef IPHONE
		System::OpenURL ((char*)url, (char*)target);
		#endif

	}


	bool lime_system_set_allow_screen_timeout (bool allow) {

		return System::SetAllowScreenTimeout (allow);

	}


	HL_PRIM bool HL_NAME(hl_system_set_allow_screen_timeout) (bool allow) {

		return System::SetAllowScreenTimeout (allow);

	}


	bool lime_system_set_windows_console_mode (int handleType, int mode) {

		#if defined (HX_WINDOWS) && !defined (HX_WINRT)
		return System::SetWindowsConsoleMode (handleType, mode);
		#else
		return false;
		#endif

	}


	HL_PRIM bool HL_NAME(hl_system_set_windows_console_mode) (int handleType, int mode) {

		#if defined (HX_WINDOWS) && !defined (HX_WINRT)
		return System::SetWindowsConsoleMode (handleType, mode);
		#else
		return false;
		#endif

	}


	void lime_text_event_manager_register (value callback, value eventObject) {

		TextEvent::callback = new ValuePointer (callback);
		TextEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_text_event_manager_register) (vclosure* callback, TextEvent* eventObject) {

		TextEvent::callback = new ValuePointer (callback);
		TextEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	void lime_touch_event_manager_register (value callback, value eventObject) {

		TouchEvent::callback = new ValuePointer (callback);
		TouchEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_touch_event_manager_register) (vclosure* callback, TouchEvent* eventObject) {

		TouchEvent::callback = new ValuePointer (callback);
		TouchEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	void lime_window_alert (value window, HxString message, HxString title) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->Alert (hxs_utf8 (message, nullptr), hxs_utf8 (title, nullptr));

	}


	HL_PRIM void HL_NAME(hl_window_alert) (HL_CFFIPointer* window, hl_vstring* message, hl_vstring* title) {

		Window* targetWindow = (Window*)window->ptr;
		const char *cmessage = message ? hl_to_utf8(message->bytes) : nullptr;
		const char *ctitle = title ? hl_to_utf8(title->bytes) : nullptr;
		targetWindow->Alert (cmessage, ctitle);

	}


	void lime_window_close (value window) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->Close ();

	}


	HL_PRIM void HL_NAME(hl_window_close) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->Close ();

	}


	void lime_window_context_flip (value window) {

		((Window*)val_data (window))->ContextFlip ();

	}


	HL_PRIM void HL_NAME(hl_window_context_flip) (HL_CFFIPointer* window) {

		((Window*)window->ptr)->ContextFlip ();

	}


	value lime_window_context_lock (value window) {

		return (value)((Window*)val_data (window))->ContextLock (true);

	}


	HL_PRIM vdynamic* HL_NAME(hl_window_context_lock) (HL_CFFIPointer* window) {

		return (vdynamic*)((Window*)window->ptr)->ContextLock (false);

	}


	void lime_window_context_make_current (value window) {

		((Window*)val_data (window))->ContextMakeCurrent ();

	}


	HL_PRIM void HL_NAME(hl_window_context_make_current) (HL_CFFIPointer* window) {

		((Window*)window->ptr)->ContextMakeCurrent ();

	}


	void lime_window_context_unlock (value window) {

		((Window*)val_data (window))->ContextUnlock ();

	}


	HL_PRIM void HL_NAME(hl_window_context_unlock) (HL_CFFIPointer* window) {

		((Window*)window->ptr)->ContextUnlock ();

	}


	value lime_window_create (value application, int width, int height, int flags, HxString title) {

		Window* window = CreateWindow ((Application*)val_data (application), width, height, flags, hxs_utf8 (title, nullptr));
		return CFFIPointer (window, gc_window);

	}


	HL_PRIM HL_CFFIPointer* HL_NAME(hl_window_create) (HL_CFFIPointer* application, int width, int height, int flags, hl_vstring* title) {

		Window* window = CreateWindow ((Application*)application->ptr, width, height, flags, (const char*)hl_to_utf8 ((const uchar*)title->bytes));
		return HLCFFIPointer (window, (hl_finalizer)hl_gc_window);

	}


	void lime_window_event_manager_register (value callback, value eventObject) {

		WindowEvent::callback = new ValuePointer (callback);
		WindowEvent::eventObject = new ValuePointer (eventObject);

	}


	HL_PRIM void HL_NAME(hl_window_event_manager_register) (vclosure* callback, WindowEvent* eventObject) {

		WindowEvent::callback = new ValuePointer (callback);
		WindowEvent::eventObject = new ValuePointer ((vobj*)eventObject);

	}


	void lime_window_focus (value window) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->Focus ();

	}


	HL_PRIM void HL_NAME(hl_window_focus) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->Focus ();

	}


	double lime_window_get_context (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return (uintptr_t)targetWindow->GetContext ();

	}


	HL_PRIM double HL_NAME(hl_window_get_context) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return (uintptr_t)targetWindow->GetContext ();

	}


	value lime_window_get_context_type (value window) {

		Window* targetWindow = (Window*)val_data (window);
		const char* type = targetWindow->GetContextType ();
		return type ? alloc_string (type) : alloc_null ();

	}


	HL_PRIM vbyte* HL_NAME(hl_window_get_context_type) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return (vbyte*)targetWindow->GetContextType ();

	}


	value lime_window_get_vulkan_instance_extensions (value window) {

		Window* targetWindow = (Window*)val_data (window);
		unsigned int count = 0;

		if (!targetWindow->GetVulkanInstanceExtensions (&count, 0) || count == 0) {

			return alloc_array (0);

		}

		std::vector<const char*> names (count);
		if (!targetWindow->GetVulkanInstanceExtensions (&count, names.data ())) {

			return alloc_array (0);

		}

		value result = alloc_array (count);

		for (unsigned int i = 0; i < count; ++i) {

			val_array_set_i (result, i, alloc_string (names[i]));

		}

		return result;

	}


	HL_PRIM hl_varray* HL_NAME(hl_window_get_vulkan_instance_extensions) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		unsigned int count = 0;

		if (!targetWindow->GetVulkanInstanceExtensions (&count, 0) || count == 0) {

			return hl_alloc_array (&hlt_bytes, 0);

		}

		std::vector<const char*> names (count);
		if (!targetWindow->GetVulkanInstanceExtensions (&count, names.data ())) {

			return hl_alloc_array (&hlt_bytes, 0);

		}

		hl_varray* result = (hl_varray*)hl_alloc_array (&hlt_bytes, count);
		vbyte** resultData = hl_aptr (result, vbyte*);

		for (unsigned int i = 0; i < count; ++i) {

			*resultData++ = hl_copy_bytes ((const vbyte*)names[i], (int)std::strlen (names[i]) + 1);

		}

		return result;

	}


	value lime_window_get_vulkan_drawable_size (value window) {

		Window* targetWindow = (Window*)val_data (window);
		int width = 0;
		int height = 0;
		targetWindow->GetVulkanDrawableSize (&width, &height);

		value result = alloc_empty_object ();
		alloc_field (result, val_id ("width"), alloc_int (width));
		alloc_field (result, val_id ("height"), alloc_int (height));
		return result;

	}


	HL_PRIM vdynamic* HL_NAME(hl_window_get_vulkan_drawable_size) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		int width = 0;
		int height = 0;
		targetWindow->GetVulkanDrawableSize (&width, &height);

		const int id_width = hl_hash_utf8 ("width");
		const int id_height = hl_hash_utf8 ("height");
		vdynamic* result = (vdynamic*)hl_alloc_dynobj();
		hl_dyn_seti (result, id_width, &hlt_i32, width);
		hl_dyn_seti (result, id_height, &hlt_i32, height);
		return result;

	}


	value lime_window_get_vulkan_instance_proc_addr (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return CreateVulkanHandleValue ((uint64_t)(uintptr_t)targetWindow->GetVulkanInstanceProcAddr ());

	}


	HL_PRIM vdynamic* HL_NAME(hl_window_get_vulkan_instance_proc_addr) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return HLCreateVulkanHandleValue ((uint64_t)(uintptr_t)targetWindow->GetVulkanInstanceProcAddr ());

	}


	value lime_window_create_vulkan_surface (value window, int instanceHigh, int instanceLow) {

		Window* targetWindow = (Window*)val_data (window);
		uint64_t surface = targetWindow->CreateVulkanSurface ((uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow));

		if (!surface) {

			return alloc_null ();

		}

		return CreateVulkanHandleValue (surface);

	}


	HL_PRIM vdynamic* HL_NAME(hl_window_create_vulkan_surface) (HL_CFFIPointer* window, int instanceHigh, int instanceLow) {

		Window* targetWindow = (Window*)window->ptr;
		uint64_t surface = targetWindow->CreateVulkanSurface ((uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow));

		if (!surface) {

			return 0;

		}

		return HLCreateVulkanHandleValue (surface);

	}


	value lime_vk_create_instance (value window, HxString applicationName) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = VK_NULL_HANDLE;

		if (!CreateManagedVulkanInstance (targetWindow, applicationName.c_str () ? hxs_utf8 (applicationName, nullptr) : 0, &instance)) {

			return alloc_null ();

		}

		return CreateVulkanHandleValue ((uint64_t)(uintptr_t)instance);
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return alloc_null ();
#endif

	}


	HL_PRIM vdynamic* HL_NAME(hl_vk_create_instance) (HL_CFFIPointer* window, hl_vstring* applicationName) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = VK_NULL_HANDLE;
		const char* applicationNameUTF8 = applicationName ? (const char*)hl_to_utf8 ((const uchar*)applicationName->bytes) : 0;

		if (!CreateManagedVulkanInstance (targetWindow, applicationNameUTF8, &instance)) {

			return 0;

		}

		return HLCreateVulkanHandleValue ((uint64_t)(uintptr_t)instance);
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return 0;
#endif

	}


	void lime_vk_destroy_instance (value window, int instanceHigh, int instanceLow) {

#ifdef LIME_VULKAN
		if (instanceHigh == 0 && instanceLow == 0) return;
		Window* targetWindow = (Window*)val_data (window);
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = targetWindow ? (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr () : 0;
		if (!vkGetInstanceProcAddr) return;

		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		PFN_vkDestroyInstance vkDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr (instance, "vkDestroyInstance");
		if (vkDestroyInstance) {

			vkDestroyInstance (instance, 0);

		}
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	HL_PRIM void HL_NAME(hl_vk_destroy_instance) (HL_CFFIPointer* window, int instanceHigh, int instanceLow) {

#ifdef LIME_VULKAN
		if (instanceHigh == 0 && instanceLow == 0) return;
		Window* targetWindow = (Window*)window->ptr;
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = targetWindow ? (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr () : 0;
		if (!vkGetInstanceProcAddr) return;

		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		PFN_vkDestroyInstance vkDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr (instance, "vkDestroyInstance");
		if (vkDestroyInstance) {

			vkDestroyInstance (instance, 0);

		}
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	void lime_vk_destroy_surface (value window, int instanceHigh, int instanceLow, int surfaceHigh, int surfaceLow) {

#ifdef LIME_VULKAN
		if (instanceHigh == 0 && instanceLow == 0) return;
		Window* targetWindow = (Window*)val_data (window);
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = targetWindow ? (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr () : 0;
		if (!vkGetInstanceProcAddr) return;

		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkSurfaceKHR surface = UInt64ToVulkanSurface (CombineVulkanHandle (surfaceHigh, surfaceLow));
		if (!surface) return;

		PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr (instance, "vkDestroySurfaceKHR");
		if (vkDestroySurfaceKHR) {

			vkDestroySurfaceKHR (instance, surface, 0);

		}
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	HL_PRIM void HL_NAME(hl_vk_destroy_surface) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int surfaceHigh, int surfaceLow) {

#ifdef LIME_VULKAN
		if (instanceHigh == 0 && instanceLow == 0) return;
		Window* targetWindow = (Window*)window->ptr;
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = targetWindow ? (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr () : 0;
		if (!vkGetInstanceProcAddr) return;

		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkSurfaceKHR surface = UInt64ToVulkanSurface (CombineVulkanHandle (surfaceHigh, surfaceLow));
		if (!surface) return;

		PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr (instance, "vkDestroySurfaceKHR");
		if (vkDestroySurfaceKHR) {

			vkDestroySurfaceKHR (instance, surface, 0);

		}
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	value lime_vk_get_physical_devices (value window, int instanceHigh, int instanceLow, int surfaceHigh, int surfaceLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);

		if (!targetWindow || !instance) {

			lastVKError = "Missing Vulkan window or instance";
			return alloc_null ();

		}

		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr ();
		if (!vkGetInstanceProcAddr) {

			lastVKError = "SDL did not expose vkGetInstanceProcAddr";
			return alloc_null ();

		}

		PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr (instance,
			"vkEnumeratePhysicalDevices");
		PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr (instance,
			"vkGetPhysicalDeviceProperties");
		PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties =
			(PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr (instance, "vkGetPhysicalDeviceQueueFamilyProperties");
		PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR =
			(PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vkGetInstanceProcAddr (instance, "vkGetPhysicalDeviceSurfaceSupportKHR");

		if (!vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceProperties || !vkGetPhysicalDeviceQueueFamilyProperties) {

			lastVKError = "Missing required Vulkan instance functions";
			return alloc_null ();

		}

		VkSurfaceKHR surface = UInt64ToVulkanSurface (CombineVulkanHandle (surfaceHigh, surfaceLow));
		uint32_t deviceCount = 0;
		VkResult result = vkEnumeratePhysicalDevices (instance, &deviceCount, 0);

		if (result != VK_SUCCESS) {

			lastVKError = "vkEnumeratePhysicalDevices failed";
			return alloc_null ();

		}

		value devices = alloc_array (deviceCount);
		if (deviceCount == 0) {

			lastVKError.clear ();
			return devices;

		}

		std::vector<VkPhysicalDevice> physicalDevices (deviceCount);
		result = vkEnumeratePhysicalDevices (instance, &deviceCount, physicalDevices.data ());

		if (result != VK_SUCCESS) {

			lastVKError = "vkEnumeratePhysicalDevices returned incomplete data";
			return alloc_null ();

		}

		const int id_handle = val_id ("handle");
		const int id_name = val_id ("name");
		const int id_apiVersion = val_id ("apiVersion");
		const int id_driverVersion = val_id ("driverVersion");
		const int id_vendorID = val_id ("vendorID");
		const int id_deviceID = val_id ("deviceID");
		const int id_deviceType = val_id ("deviceType");
		const int id_queueFamilies = val_id ("queueFamilies");

		const int id_index = val_id ("index");
		const int id_flags = val_id ("flags");
		const int id_queueCount = val_id ("queueCount");
		const int id_timestampValidBits = val_id ("timestampValidBits");
		const int id_supportsGraphics = val_id ("supportsGraphics");
		const int id_supportsCompute = val_id ("supportsCompute");
		const int id_supportsTransfer = val_id ("supportsTransfer");
		const int id_supportsPresent = val_id ("supportsPresent");

		for (uint32_t i = 0; i < deviceCount; ++i) {

			VkPhysicalDeviceProperties properties;
			memset (&properties, 0, sizeof (properties));
			vkGetPhysicalDeviceProperties (physicalDevices[i], &properties);

			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties (physicalDevices[i], &queueFamilyCount, 0);
			std::vector<VkQueueFamilyProperties> queueFamilies (queueFamilyCount);
			if (queueFamilyCount > 0) {

				vkGetPhysicalDeviceQueueFamilyProperties (physicalDevices[i], &queueFamilyCount, queueFamilies.data ());

			}

			value queueFamilyArray = alloc_array (queueFamilyCount);

			for (uint32_t q = 0; q < queueFamilyCount; ++q) {

				VkBool32 supportsPresent = VK_FALSE;
				if (surface && vkGetPhysicalDeviceSurfaceSupportKHR) {

					vkGetPhysicalDeviceSurfaceSupportKHR (physicalDevices[i], q, surface, &supportsPresent);

				}

				value queueFamilyObject = alloc_empty_object ();
				alloc_field (queueFamilyObject, id_index, alloc_int (q));
				alloc_field (queueFamilyObject, id_flags, alloc_int (queueFamilies[q].queueFlags));
				alloc_field (queueFamilyObject, id_queueCount, alloc_int (queueFamilies[q].queueCount));
				alloc_field (queueFamilyObject, id_timestampValidBits, alloc_int (queueFamilies[q].timestampValidBits));
				alloc_field (queueFamilyObject, id_supportsGraphics, alloc_bool ((queueFamilies[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0));
				alloc_field (queueFamilyObject, id_supportsCompute, alloc_bool ((queueFamilies[q].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0));
				alloc_field (queueFamilyObject, id_supportsTransfer, alloc_bool ((queueFamilies[q].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0));
				alloc_field (queueFamilyObject, id_supportsPresent, alloc_bool (supportsPresent != VK_FALSE));
				val_array_set_i (queueFamilyArray, q, queueFamilyObject);

			}

			value deviceObject = alloc_empty_object ();
			alloc_field (deviceObject, id_handle, CreateVulkanHandleValue ((uint64_t)(uintptr_t)physicalDevices[i]));
			alloc_field (deviceObject, id_name, alloc_string (properties.deviceName));
			alloc_field (deviceObject, id_apiVersion, alloc_int (properties.apiVersion));
			alloc_field (deviceObject, id_driverVersion, alloc_int (properties.driverVersion));
			alloc_field (deviceObject, id_vendorID, alloc_int (properties.vendorID));
			alloc_field (deviceObject, id_deviceID, alloc_int (properties.deviceID));
			alloc_field (deviceObject, id_deviceType, alloc_int (properties.deviceType));
			alloc_field (deviceObject, id_queueFamilies, queueFamilyArray);
			val_array_set_i (devices, i, deviceObject);

		}

		lastVKError.clear ();
		return devices;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return alloc_array (0);
#endif

	}


	HL_PRIM hl_varray* HL_NAME(hl_vk_get_physical_devices) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int surfaceHigh, int surfaceLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);

		if (!targetWindow || !instance) {

			lastVKError = "Missing Vulkan window or instance";
			return 0;

		}

		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr ();
		if (!vkGetInstanceProcAddr) {

			lastVKError = "SDL did not expose vkGetInstanceProcAddr";
			return 0;

		}

		PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr (instance,
			"vkEnumeratePhysicalDevices");
		PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr (instance,
			"vkGetPhysicalDeviceProperties");
		PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties =
			(PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr (instance, "vkGetPhysicalDeviceQueueFamilyProperties");
		PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR =
			(PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vkGetInstanceProcAddr (instance, "vkGetPhysicalDeviceSurfaceSupportKHR");

		if (!vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceProperties || !vkGetPhysicalDeviceQueueFamilyProperties) {

			lastVKError = "Missing required Vulkan instance functions";
			return 0;

		}

		VkSurfaceKHR surface = UInt64ToVulkanSurface (CombineVulkanHandle (surfaceHigh, surfaceLow));
		uint32_t deviceCount = 0;
		VkResult result = vkEnumeratePhysicalDevices (instance, &deviceCount, 0);

		if (result != VK_SUCCESS) {

			lastVKError = "vkEnumeratePhysicalDevices failed";
			return 0;

		}

		hl_varray* devices = (hl_varray*)hl_alloc_array (&hlt_dynobj, deviceCount);
		if (deviceCount == 0) {

			lastVKError.clear ();
			return devices;

		}

		std::vector<VkPhysicalDevice> physicalDevices (deviceCount);
		result = vkEnumeratePhysicalDevices (instance, &deviceCount, physicalDevices.data ());

		if (result != VK_SUCCESS) {

			lastVKError = "vkEnumeratePhysicalDevices returned incomplete data";
			return 0;

		}

		vdynamic** deviceData = hl_aptr (devices, vdynamic*);

		const int id_handle = hl_hash_utf8 ("handle");
		const int id_name = hl_hash_utf8 ("name");
		const int id_apiVersion = hl_hash_utf8 ("apiVersion");
		const int id_driverVersion = hl_hash_utf8 ("driverVersion");
		const int id_vendorID = hl_hash_utf8 ("vendorID");
		const int id_deviceID = hl_hash_utf8 ("deviceID");
		const int id_deviceType = hl_hash_utf8 ("deviceType");
		const int id_queueFamilies = hl_hash_utf8 ("queueFamilies");

		const int id_index = hl_hash_utf8 ("index");
		const int id_flags = hl_hash_utf8 ("flags");
		const int id_queueCount = hl_hash_utf8 ("queueCount");
		const int id_timestampValidBits = hl_hash_utf8 ("timestampValidBits");
		const int id_supportsGraphics = hl_hash_utf8 ("supportsGraphics");
		const int id_supportsCompute = hl_hash_utf8 ("supportsCompute");
		const int id_supportsTransfer = hl_hash_utf8 ("supportsTransfer");
		const int id_supportsPresent = hl_hash_utf8 ("supportsPresent");

		for (uint32_t i = 0; i < deviceCount; ++i) {

			VkPhysicalDeviceProperties properties;
			memset (&properties, 0, sizeof (properties));
			vkGetPhysicalDeviceProperties (physicalDevices[i], &properties);

			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties (physicalDevices[i], &queueFamilyCount, 0);
			std::vector<VkQueueFamilyProperties> queueFamilies (queueFamilyCount);
			if (queueFamilyCount > 0) {

				vkGetPhysicalDeviceQueueFamilyProperties (physicalDevices[i], &queueFamilyCount, queueFamilies.data ());

			}

			hl_varray* queueFamilyArray = (hl_varray*)hl_alloc_array (&hlt_dynobj, queueFamilyCount);
			vdynamic** queueFamilyData = hl_aptr (queueFamilyArray, vdynamic*);

			for (uint32_t q = 0; q < queueFamilyCount; ++q) {

				VkBool32 supportsPresent = VK_FALSE;
				if (surface && vkGetPhysicalDeviceSurfaceSupportKHR) {

					vkGetPhysicalDeviceSurfaceSupportKHR (physicalDevices[i], q, surface, &supportsPresent);

				}

				vdynamic* queueFamilyObject = (vdynamic*)hl_alloc_dynobj ();
				*queueFamilyData++ = queueFamilyObject;
				hl_dyn_seti (queueFamilyObject, id_index, &hlt_i32, q);
				hl_dyn_seti (queueFamilyObject, id_flags, &hlt_i32, queueFamilies[q].queueFlags);
				hl_dyn_seti (queueFamilyObject, id_queueCount, &hlt_i32, queueFamilies[q].queueCount);
				hl_dyn_seti (queueFamilyObject, id_timestampValidBits, &hlt_i32, queueFamilies[q].timestampValidBits);
				hl_dyn_seti (queueFamilyObject, id_supportsGraphics, &hlt_bool, (queueFamilies[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0);
				hl_dyn_seti (queueFamilyObject, id_supportsCompute, &hlt_bool, (queueFamilies[q].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0);
				hl_dyn_seti (queueFamilyObject, id_supportsTransfer, &hlt_bool, (queueFamilies[q].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0);
				hl_dyn_seti (queueFamilyObject, id_supportsPresent, &hlt_bool, supportsPresent != VK_FALSE);

			}

			vdynamic* deviceObject = (vdynamic*)hl_alloc_dynobj ();
			*deviceData++ = deviceObject;
			hl_dyn_setp (deviceObject, id_handle, &hlt_dynobj, HLCreateVulkanHandleValue ((uint64_t)(uintptr_t)physicalDevices[i]));
			hl_dyn_setp (deviceObject, id_name, &hlt_bytes, hl_copy_bytes ((const vbyte*)properties.deviceName, (int)std::strlen (properties.deviceName) + 1));
			hl_dyn_seti (deviceObject, id_apiVersion, &hlt_i32, properties.apiVersion);
			hl_dyn_seti (deviceObject, id_driverVersion, &hlt_i32, properties.driverVersion);
			hl_dyn_seti (deviceObject, id_vendorID, &hlt_i32, properties.vendorID);
			hl_dyn_seti (deviceObject, id_deviceID, &hlt_i32, properties.deviceID);
			hl_dyn_seti (deviceObject, id_deviceType, &hlt_i32, properties.deviceType);
			hl_dyn_setp (deviceObject, id_queueFamilies, &hlt_array, queueFamilyArray);

		}

		lastVKError.clear ();
		return devices;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return (hl_varray*)hl_alloc_array (&hlt_dynobj, 0);
#endif

	}


	value lime_vk_create_device (value window, int instanceHigh, int instanceLow, int physicalDeviceHigh, int physicalDeviceLow, int queueFamilyIndex,
		value extensions) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkPhysicalDevice physicalDevice = (VkPhysicalDevice)(uintptr_t)CombineVulkanHandle (physicalDeviceHigh, physicalDeviceLow);
		std::vector<std::string> deviceExtensions = GetVulkanDeviceExtensions (extensions);
		VkDevice device = VK_NULL_HANDLE;
		VkQueue queue = VK_NULL_HANDLE;

		if (!CreateManagedVulkanDevice (targetWindow, instance, physicalDevice, queueFamilyIndex, deviceExtensions, &device, &queue)) {

			return alloc_null ();

		}

		value result = alloc_empty_object ();
		alloc_field (result, val_id ("handle"), CreateVulkanHandleValue ((uint64_t)(uintptr_t)device));
		alloc_field (result, val_id ("queue"), CreateVulkanHandleValue ((uint64_t)(uintptr_t)queue));
		alloc_field (result, val_id ("queueFamilyIndex"), alloc_int (queueFamilyIndex));
		return result;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return alloc_null ();
#endif

	}


	HL_PRIM vdynamic* HL_NAME(hl_vk_create_device) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int physicalDeviceHigh,
		int physicalDeviceLow, int queueFamilyIndex, hl_varray* extensions) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkPhysicalDevice physicalDevice = (VkPhysicalDevice)(uintptr_t)CombineVulkanHandle (physicalDeviceHigh, physicalDeviceLow);
		std::vector<std::string> deviceExtensions = GetHLVulkanDeviceExtensions (extensions);
		VkDevice device = VK_NULL_HANDLE;
		VkQueue queue = VK_NULL_HANDLE;

		if (!CreateManagedVulkanDevice (targetWindow, instance, physicalDevice, queueFamilyIndex, deviceExtensions, &device, &queue)) {

			return 0;

		}

		const int id_handle = hl_hash_utf8 ("handle");
		const int id_queue = hl_hash_utf8 ("queue");
		const int id_queueFamilyIndex = hl_hash_utf8 ("queueFamilyIndex");
		vdynamic* result = (vdynamic*)hl_alloc_dynobj ();
		hl_dyn_setp (result, id_handle, &hlt_dynobj, HLCreateVulkanHandleValue ((uint64_t)(uintptr_t)device));
		hl_dyn_setp (result, id_queue, &hlt_dynobj, HLCreateVulkanHandleValue ((uint64_t)(uintptr_t)queue));
		hl_dyn_seti (result, id_queueFamilyIndex, &hlt_i32, queueFamilyIndex);
		return result;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return 0;
#endif

	}


	void lime_vk_destroy_device (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow) {

#ifdef LIME_VULKAN
		if (deviceHigh == 0 && deviceLow == 0) return;
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = targetWindow ? (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr () : 0;
		if (!vkGetInstanceProcAddr) return;

		PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr (instance, "vkGetDeviceProcAddr");
		if (!vkGetDeviceProcAddr) return;

		PFN_vkDestroyDevice vkDestroyDevice = (PFN_vkDestroyDevice)vkGetDeviceProcAddr (device, "vkDestroyDevice");
		if (vkDestroyDevice) {

			vkDestroyDevice (device, 0);

		}
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	HL_PRIM void HL_NAME(hl_vk_destroy_device) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow) {

#ifdef LIME_VULKAN
		if (deviceHigh == 0 && deviceLow == 0) return;
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = targetWindow ? (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr () : 0;
		if (!vkGetInstanceProcAddr) return;

		PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr (instance, "vkGetDeviceProcAddr");
		if (!vkGetDeviceProcAddr) return;

		PFN_vkDestroyDevice vkDestroyDevice = (PFN_vkDestroyDevice)vkGetDeviceProcAddr (device, "vkDestroyDevice");
		if (vkDestroyDevice) {

			vkDestroyDevice (device, 0);

		}
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	bool lime_vk_device_wait_idle (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = targetWindow ? (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr () : 0;
		if (!vkGetInstanceProcAddr || !instance || !device) {

			lastVKError = "Missing Vulkan window, instance, or device";
			return false;

		}

		PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr (instance, "vkGetDeviceProcAddr");
		if (!vkGetDeviceProcAddr) {

			lastVKError = "Missing Vulkan device proc address function";
			return false;

		}

		PFN_vkDeviceWaitIdle vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)vkGetDeviceProcAddr (device, "vkDeviceWaitIdle");
		if (!vkDeviceWaitIdle) {

			lastVKError = "Missing Vulkan device wait idle function";
			return false;

		}

		VkResult result = vkDeviceWaitIdle (device);
		if (result != VK_SUCCESS) {

			lastVKError = "vkDeviceWaitIdle failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	HL_PRIM bool HL_NAME(hl_vk_device_wait_idle) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = targetWindow ? (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr () : 0;
		if (!vkGetInstanceProcAddr || !instance || !device) {

			lastVKError = "Missing Vulkan window, instance, or device";
			return false;

		}

		PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr (instance, "vkGetDeviceProcAddr");
		if (!vkGetDeviceProcAddr) {

			lastVKError = "Missing Vulkan device proc address function";
			return false;

		}

		PFN_vkDeviceWaitIdle vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)vkGetDeviceProcAddr (device, "vkDeviceWaitIdle");
		if (!vkDeviceWaitIdle) {

			lastVKError = "Missing Vulkan device wait idle function";
			return false;

		}

		VkResult result = vkDeviceWaitIdle (device);
		if (result != VK_SUCCESS) {

			lastVKError = "vkDeviceWaitIdle failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	bool lime_vk_queue_wait_idle (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int queueHigh, int queueLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkQueue queue = (VkQueue)(uintptr_t)CombineVulkanHandle (queueHigh, queueLow);
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = targetWindow ? (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr () : 0;
		if (!vkGetInstanceProcAddr || !instance || !device || !queue) {

			lastVKError = "Missing Vulkan window, instance, device, or queue";
			return false;

		}

		PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr (instance, "vkGetDeviceProcAddr");
		if (!vkGetDeviceProcAddr) {

			lastVKError = "Missing Vulkan device proc address function";
			return false;

		}

		PFN_vkQueueWaitIdle vkQueueWaitIdle = (PFN_vkQueueWaitIdle)vkGetDeviceProcAddr (device, "vkQueueWaitIdle");
		if (!vkQueueWaitIdle) {

			lastVKError = "Missing Vulkan queue wait idle function";
			return false;

		}

		VkResult result = vkQueueWaitIdle (queue);
		if (result != VK_SUCCESS) {

			lastVKError = "vkQueueWaitIdle failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	HL_PRIM bool HL_NAME(hl_vk_queue_wait_idle) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int queueHigh,
		int queueLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkQueue queue = (VkQueue)(uintptr_t)CombineVulkanHandle (queueHigh, queueLow);
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = targetWindow ? (PFN_vkGetInstanceProcAddr)targetWindow->GetVulkanInstanceProcAddr () : 0;
		if (!vkGetInstanceProcAddr || !instance || !device || !queue) {

			lastVKError = "Missing Vulkan window, instance, device, or queue";
			return false;

		}

		PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr (instance, "vkGetDeviceProcAddr");
		if (!vkGetDeviceProcAddr) {

			lastVKError = "Missing Vulkan device proc address function";
			return false;

		}

		PFN_vkQueueWaitIdle vkQueueWaitIdle = (PFN_vkQueueWaitIdle)vkGetDeviceProcAddr (device, "vkQueueWaitIdle");
		if (!vkQueueWaitIdle) {

			lastVKError = "Missing Vulkan queue wait idle function";
			return false;

		}

		VkResult result = vkQueueWaitIdle (queue);
		if (result != VK_SUCCESS) {

			lastVKError = "vkQueueWaitIdle failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	value lime_vk_create_command_pool (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int queueFamilyIndex, int flags) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		PFN_vkCreateCommandPool vkCreateCommandPool = (PFN_vkCreateCommandPool)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkCreateCommandPool");
		if (!vkCreateCommandPool) return alloc_null ();

		VkCommandPoolCreateInfo createInfo;
		memset (&createInfo, 0, sizeof (createInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		createInfo.flags = flags;
		createInfo.queueFamilyIndex = queueFamilyIndex;

		VkCommandPool commandPool = VK_NULL_HANDLE;
		VkResult result = vkCreateCommandPool (device, &createInfo, 0, &commandPool);
		if (result != VK_SUCCESS) {

			lastVKError = "vkCreateCommandPool failed";
			return alloc_null ();

		}

		lastVKError.clear ();
		return CreateVulkanHandleValue ((uint64_t)(uintptr_t)commandPool);
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return alloc_null ();
#endif

	}


	HL_PRIM vdynamic* HL_NAME(hl_vk_create_command_pool) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow,
		int queueFamilyIndex, int flags) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		PFN_vkCreateCommandPool vkCreateCommandPool = (PFN_vkCreateCommandPool)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkCreateCommandPool");
		if (!vkCreateCommandPool) return 0;

		VkCommandPoolCreateInfo createInfo;
		memset (&createInfo, 0, sizeof (createInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		createInfo.flags = flags;
		createInfo.queueFamilyIndex = queueFamilyIndex;

		VkCommandPool commandPool = VK_NULL_HANDLE;
		VkResult result = vkCreateCommandPool (device, &createInfo, 0, &commandPool);
		if (result != VK_SUCCESS) {

			lastVKError = "vkCreateCommandPool failed";
			return 0;

		}

		lastVKError.clear ();
		return HLCreateVulkanHandleValue ((uint64_t)(uintptr_t)commandPool);
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return 0;
#endif

	}


	void lime_vk_destroy_command_pool (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int commandPoolHigh,
		int commandPoolLow) {

#ifdef LIME_VULKAN
		if (commandPoolHigh == 0 && commandPoolLow == 0) return;
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandPool commandPool = (VkCommandPool)(uintptr_t)CombineVulkanHandle (commandPoolHigh, commandPoolLow);
		PFN_vkDestroyCommandPool vkDestroyCommandPool = (PFN_vkDestroyCommandPool)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkDestroyCommandPool");
		if (vkDestroyCommandPool) vkDestroyCommandPool (device, commandPool, 0);
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	HL_PRIM void HL_NAME(hl_vk_destroy_command_pool) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow,
		int commandPoolHigh, int commandPoolLow) {

#ifdef LIME_VULKAN
		if (commandPoolHigh == 0 && commandPoolLow == 0) return;
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandPool commandPool = (VkCommandPool)(uintptr_t)CombineVulkanHandle (commandPoolHigh, commandPoolLow);
		PFN_vkDestroyCommandPool vkDestroyCommandPool = (PFN_vkDestroyCommandPool)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkDestroyCommandPool");
		if (vkDestroyCommandPool) vkDestroyCommandPool (device, commandPool, 0);
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	bool lime_vk_reset_command_pool (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int commandPoolHigh,
		int commandPoolLow, int flags) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandPool commandPool = (VkCommandPool)(uintptr_t)CombineVulkanHandle (commandPoolHigh, commandPoolLow);
		PFN_vkResetCommandPool vkResetCommandPool = (PFN_vkResetCommandPool)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkResetCommandPool");
		if (!vkResetCommandPool) return false;

		VkResult result = vkResetCommandPool (device, commandPool, flags);
		if (result != VK_SUCCESS) {

			lastVKError = "vkResetCommandPool failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	HL_PRIM bool HL_NAME(hl_vk_reset_command_pool) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow,
		int commandPoolHigh, int commandPoolLow, int flags) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandPool commandPool = (VkCommandPool)(uintptr_t)CombineVulkanHandle (commandPoolHigh, commandPoolLow);
		PFN_vkResetCommandPool vkResetCommandPool = (PFN_vkResetCommandPool)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkResetCommandPool");
		if (!vkResetCommandPool) return false;

		VkResult result = vkResetCommandPool (device, commandPool, flags);
		if (result != VK_SUCCESS) {

			lastVKError = "vkResetCommandPool failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	value lime_vk_allocate_command_buffer (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int commandPoolHigh,
		int commandPoolLow, int level) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandPool commandPool = (VkCommandPool)(uintptr_t)CombineVulkanHandle (commandPoolHigh, commandPoolLow);
		PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkAllocateCommandBuffers");
		if (!vkAllocateCommandBuffers) return alloc_null ();

		VkCommandBufferAllocateInfo allocateInfo;
		memset (&allocateInfo, 0, sizeof (allocateInfo));
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = commandPool;
		allocateInfo.level = (VkCommandBufferLevel)level;
		allocateInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkResult result = vkAllocateCommandBuffers (device, &allocateInfo, &commandBuffer);
		if (result != VK_SUCCESS) {

			lastVKError = "vkAllocateCommandBuffers failed";
			return alloc_null ();

		}

		lastVKError.clear ();
		return CreateVulkanHandleValue ((uint64_t)(uintptr_t)commandBuffer);
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return alloc_null ();
#endif

	}


	HL_PRIM vdynamic* HL_NAME(hl_vk_allocate_command_buffer) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh,
		int deviceLow, int commandPoolHigh, int commandPoolLow, int level) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandPool commandPool = (VkCommandPool)(uintptr_t)CombineVulkanHandle (commandPoolHigh, commandPoolLow);
		PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkAllocateCommandBuffers");
		if (!vkAllocateCommandBuffers) return 0;

		VkCommandBufferAllocateInfo allocateInfo;
		memset (&allocateInfo, 0, sizeof (allocateInfo));
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = commandPool;
		allocateInfo.level = (VkCommandBufferLevel)level;
		allocateInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkResult result = vkAllocateCommandBuffers (device, &allocateInfo, &commandBuffer);
		if (result != VK_SUCCESS) {

			lastVKError = "vkAllocateCommandBuffers failed";
			return 0;

		}

		lastVKError.clear ();
		return HLCreateVulkanHandleValue ((uint64_t)(uintptr_t)commandBuffer);
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return 0;
#endif

	}


	void lime_vk_free_command_buffer (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int commandPoolHigh,
		int commandPoolLow, int commandBufferHigh, int commandBufferLow) {

#ifdef LIME_VULKAN
		if (commandBufferHigh == 0 && commandBufferLow == 0) return;
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandPool commandPool = (VkCommandPool)(uintptr_t)CombineVulkanHandle (commandPoolHigh, commandPoolLow);
		VkCommandBuffer commandBuffer = (VkCommandBuffer)(uintptr_t)CombineVulkanHandle (commandBufferHigh, commandBufferLow);
		PFN_vkFreeCommandBuffers vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkFreeCommandBuffers");
		if (vkFreeCommandBuffers) vkFreeCommandBuffers (device, commandPool, 1, &commandBuffer);
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	HL_PRIM void HL_NAME(hl_vk_free_command_buffer) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow,
		int commandPoolHigh, int commandPoolLow, int commandBufferHigh, int commandBufferLow) {

#ifdef LIME_VULKAN
		if (commandBufferHigh == 0 && commandBufferLow == 0) return;
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandPool commandPool = (VkCommandPool)(uintptr_t)CombineVulkanHandle (commandPoolHigh, commandPoolLow);
		VkCommandBuffer commandBuffer = (VkCommandBuffer)(uintptr_t)CombineVulkanHandle (commandBufferHigh, commandBufferLow);
		PFN_vkFreeCommandBuffers vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkFreeCommandBuffers");
		if (vkFreeCommandBuffers) vkFreeCommandBuffers (device, commandPool, 1, &commandBuffer);
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	bool lime_vk_begin_command_buffer (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int commandBufferHigh,
		int commandBufferLow, int flags) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandBuffer commandBuffer = (VkCommandBuffer)(uintptr_t)CombineVulkanHandle (commandBufferHigh, commandBufferLow);
		PFN_vkBeginCommandBuffer vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkBeginCommandBuffer");
		if (!vkBeginCommandBuffer) return false;

		VkCommandBufferBeginInfo beginInfo;
		memset (&beginInfo, 0, sizeof (beginInfo));
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = flags;

		VkResult result = vkBeginCommandBuffer (commandBuffer, &beginInfo);
		if (result != VK_SUCCESS) {

			lastVKError = "vkBeginCommandBuffer failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	HL_PRIM bool HL_NAME(hl_vk_begin_command_buffer) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow,
		int commandBufferHigh, int commandBufferLow, int flags) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandBuffer commandBuffer = (VkCommandBuffer)(uintptr_t)CombineVulkanHandle (commandBufferHigh, commandBufferLow);
		PFN_vkBeginCommandBuffer vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkBeginCommandBuffer");
		if (!vkBeginCommandBuffer) return false;

		VkCommandBufferBeginInfo beginInfo;
		memset (&beginInfo, 0, sizeof (beginInfo));
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = flags;

		VkResult result = vkBeginCommandBuffer (commandBuffer, &beginInfo);
		if (result != VK_SUCCESS) {

			lastVKError = "vkBeginCommandBuffer failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	bool lime_vk_end_command_buffer (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int commandBufferHigh,
		int commandBufferLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandBuffer commandBuffer = (VkCommandBuffer)(uintptr_t)CombineVulkanHandle (commandBufferHigh, commandBufferLow);
		PFN_vkEndCommandBuffer vkEndCommandBuffer = (PFN_vkEndCommandBuffer)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkEndCommandBuffer");
		if (!vkEndCommandBuffer) return false;

		VkResult result = vkEndCommandBuffer (commandBuffer);
		if (result != VK_SUCCESS) {

			lastVKError = "vkEndCommandBuffer failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	HL_PRIM bool HL_NAME(hl_vk_end_command_buffer) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow,
		int commandBufferHigh, int commandBufferLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandBuffer commandBuffer = (VkCommandBuffer)(uintptr_t)CombineVulkanHandle (commandBufferHigh, commandBufferLow);
		PFN_vkEndCommandBuffer vkEndCommandBuffer = (PFN_vkEndCommandBuffer)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkEndCommandBuffer");
		if (!vkEndCommandBuffer) return false;

		VkResult result = vkEndCommandBuffer (commandBuffer);
		if (result != VK_SUCCESS) {

			lastVKError = "vkEndCommandBuffer failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	bool lime_vk_reset_command_buffer (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int commandBufferHigh,
		int commandBufferLow, int flags) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandBuffer commandBuffer = (VkCommandBuffer)(uintptr_t)CombineVulkanHandle (commandBufferHigh, commandBufferLow);
		PFN_vkResetCommandBuffer vkResetCommandBuffer = (PFN_vkResetCommandBuffer)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkResetCommandBuffer");
		if (!vkResetCommandBuffer) return false;

		VkResult result = vkResetCommandBuffer (commandBuffer, flags);
		if (result != VK_SUCCESS) {

			lastVKError = "vkResetCommandBuffer failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	HL_PRIM bool HL_NAME(hl_vk_reset_command_buffer) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow,
		int commandBufferHigh, int commandBufferLow, int flags) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkCommandBuffer commandBuffer = (VkCommandBuffer)(uintptr_t)CombineVulkanHandle (commandBufferHigh, commandBufferLow);
		PFN_vkResetCommandBuffer vkResetCommandBuffer = (PFN_vkResetCommandBuffer)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkResetCommandBuffer");
		if (!vkResetCommandBuffer) return false;

		VkResult result = vkResetCommandBuffer (commandBuffer, flags);
		if (result != VK_SUCCESS) {

			lastVKError = "vkResetCommandBuffer failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	value lime_vk_create_semaphore (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		PFN_vkCreateSemaphore vkCreateSemaphore = (PFN_vkCreateSemaphore)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkCreateSemaphore");
		if (!vkCreateSemaphore) return alloc_null ();

		VkSemaphoreCreateInfo createInfo;
		memset (&createInfo, 0, sizeof (createInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkSemaphore semaphore = VK_NULL_HANDLE;
		VkResult result = vkCreateSemaphore (device, &createInfo, 0, &semaphore);
		if (result != VK_SUCCESS) {

			lastVKError = "vkCreateSemaphore failed";
			return alloc_null ();

		}

		lastVKError.clear ();
		return CreateVulkanHandleValue ((uint64_t)(uintptr_t)semaphore);
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return alloc_null ();
#endif

	}


	HL_PRIM vdynamic* HL_NAME(hl_vk_create_semaphore) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		PFN_vkCreateSemaphore vkCreateSemaphore = (PFN_vkCreateSemaphore)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkCreateSemaphore");
		if (!vkCreateSemaphore) return 0;

		VkSemaphoreCreateInfo createInfo;
		memset (&createInfo, 0, sizeof (createInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkSemaphore semaphore = VK_NULL_HANDLE;
		VkResult result = vkCreateSemaphore (device, &createInfo, 0, &semaphore);
		if (result != VK_SUCCESS) {

			lastVKError = "vkCreateSemaphore failed";
			return 0;

		}

		lastVKError.clear ();
		return HLCreateVulkanHandleValue ((uint64_t)(uintptr_t)semaphore);
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return 0;
#endif

	}


	void lime_vk_destroy_semaphore (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int semaphoreHigh,
		int semaphoreLow) {

#ifdef LIME_VULKAN
		if (semaphoreHigh == 0 && semaphoreLow == 0) return;
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkSemaphore semaphore = (VkSemaphore)(uintptr_t)CombineVulkanHandle (semaphoreHigh, semaphoreLow);
		PFN_vkDestroySemaphore vkDestroySemaphore = (PFN_vkDestroySemaphore)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkDestroySemaphore");
		if (vkDestroySemaphore) vkDestroySemaphore (device, semaphore, 0);
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	HL_PRIM void HL_NAME(hl_vk_destroy_semaphore) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow,
		int semaphoreHigh, int semaphoreLow) {

#ifdef LIME_VULKAN
		if (semaphoreHigh == 0 && semaphoreLow == 0) return;
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkSemaphore semaphore = (VkSemaphore)(uintptr_t)CombineVulkanHandle (semaphoreHigh, semaphoreLow);
		PFN_vkDestroySemaphore vkDestroySemaphore = (PFN_vkDestroySemaphore)GetManagedVulkanDeviceProc (targetWindow, instance, device,
			"vkDestroySemaphore");
		if (vkDestroySemaphore) vkDestroySemaphore (device, semaphore, 0);
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	value lime_vk_create_fence (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int flags) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		PFN_vkCreateFence vkCreateFence = (PFN_vkCreateFence)GetManagedVulkanDeviceProc (targetWindow, instance, device, "vkCreateFence");
		if (!vkCreateFence) return alloc_null ();

		VkFenceCreateInfo createInfo;
		memset (&createInfo, 0, sizeof (createInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		createInfo.flags = flags;

		VkFence fence = VK_NULL_HANDLE;
		VkResult result = vkCreateFence (device, &createInfo, 0, &fence);
		if (result != VK_SUCCESS) {

			lastVKError = "vkCreateFence failed";
			return alloc_null ();

		}

		lastVKError.clear ();
		return CreateVulkanHandleValue ((uint64_t)(uintptr_t)fence);
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return alloc_null ();
#endif

	}


	HL_PRIM vdynamic* HL_NAME(hl_vk_create_fence) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow,
		int flags) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		PFN_vkCreateFence vkCreateFence = (PFN_vkCreateFence)GetManagedVulkanDeviceProc (targetWindow, instance, device, "vkCreateFence");
		if (!vkCreateFence) return 0;

		VkFenceCreateInfo createInfo;
		memset (&createInfo, 0, sizeof (createInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		createInfo.flags = flags;

		VkFence fence = VK_NULL_HANDLE;
		VkResult result = vkCreateFence (device, &createInfo, 0, &fence);
		if (result != VK_SUCCESS) {

			lastVKError = "vkCreateFence failed";
			return 0;

		}

		lastVKError.clear ();
		return HLCreateVulkanHandleValue ((uint64_t)(uintptr_t)fence);
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return 0;
#endif

	}


	void lime_vk_destroy_fence (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int fenceHigh, int fenceLow) {

#ifdef LIME_VULKAN
		if (fenceHigh == 0 && fenceLow == 0) return;
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkFence fence = (VkFence)(uintptr_t)CombineVulkanHandle (fenceHigh, fenceLow);
		PFN_vkDestroyFence vkDestroyFence = (PFN_vkDestroyFence)GetManagedVulkanDeviceProc (targetWindow, instance, device, "vkDestroyFence");
		if (vkDestroyFence) vkDestroyFence (device, fence, 0);
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	HL_PRIM void HL_NAME(hl_vk_destroy_fence) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int fenceHigh,
		int fenceLow) {

#ifdef LIME_VULKAN
		if (fenceHigh == 0 && fenceLow == 0) return;
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkFence fence = (VkFence)(uintptr_t)CombineVulkanHandle (fenceHigh, fenceLow);
		PFN_vkDestroyFence vkDestroyFence = (PFN_vkDestroyFence)GetManagedVulkanDeviceProc (targetWindow, instance, device, "vkDestroyFence");
		if (vkDestroyFence) vkDestroyFence (device, fence, 0);
#else
		lastVKError = "Lime was built without lime-vulkan support";
#endif

	}


	bool lime_vk_wait_for_fence (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int fenceHigh, int fenceLow,
		int timeoutHigh, int timeoutLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkFence fence = (VkFence)(uintptr_t)CombineVulkanHandle (fenceHigh, fenceLow);
		uint64_t timeout = CombineVulkanHandle (timeoutHigh, timeoutLow);
		PFN_vkWaitForFences vkWaitForFences = (PFN_vkWaitForFences)GetManagedVulkanDeviceProc (targetWindow, instance, device, "vkWaitForFences");
		if (!vkWaitForFences) return false;

		VkResult result = vkWaitForFences (device, 1, &fence, VK_TRUE, timeout);
		if (result != VK_SUCCESS) {

			lastVKError = "vkWaitForFences failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	HL_PRIM bool HL_NAME(hl_vk_wait_for_fence) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int fenceHigh,
		int fenceLow, int timeoutHigh, int timeoutLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkFence fence = (VkFence)(uintptr_t)CombineVulkanHandle (fenceHigh, fenceLow);
		uint64_t timeout = CombineVulkanHandle (timeoutHigh, timeoutLow);
		PFN_vkWaitForFences vkWaitForFences = (PFN_vkWaitForFences)GetManagedVulkanDeviceProc (targetWindow, instance, device, "vkWaitForFences");
		if (!vkWaitForFences) return false;

		VkResult result = vkWaitForFences (device, 1, &fence, VK_TRUE, timeout);
		if (result != VK_SUCCESS) {

			lastVKError = "vkWaitForFences failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	bool lime_vk_reset_fence (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int fenceHigh, int fenceLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkFence fence = (VkFence)(uintptr_t)CombineVulkanHandle (fenceHigh, fenceLow);
		PFN_vkResetFences vkResetFences = (PFN_vkResetFences)GetManagedVulkanDeviceProc (targetWindow, instance, device, "vkResetFences");
		if (!vkResetFences) return false;

		VkResult result = vkResetFences (device, 1, &fence);
		if (result != VK_SUCCESS) {

			lastVKError = "vkResetFences failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	HL_PRIM bool HL_NAME(hl_vk_reset_fence) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int fenceHigh,
		int fenceLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkFence fence = (VkFence)(uintptr_t)CombineVulkanHandle (fenceHigh, fenceLow);
		PFN_vkResetFences vkResetFences = (PFN_vkResetFences)GetManagedVulkanDeviceProc (targetWindow, instance, device, "vkResetFences");
		if (!vkResetFences) return false;

		VkResult result = vkResetFences (device, 1, &fence);
		if (result != VK_SUCCESS) {

			lastVKError = "vkResetFences failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	bool lime_vk_queue_submit (value window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int queueHigh, int queueLow,
		int commandBufferHigh, int commandBufferLow, int fenceHigh, int fenceLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)val_data (window);
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkQueue queue = (VkQueue)(uintptr_t)CombineVulkanHandle (queueHigh, queueLow);
		VkCommandBuffer commandBuffer = (VkCommandBuffer)(uintptr_t)CombineVulkanHandle (commandBufferHigh, commandBufferLow);
		VkFence fence = (VkFence)(uintptr_t)CombineVulkanHandle (fenceHigh, fenceLow);
		PFN_vkQueueSubmit vkQueueSubmit = (PFN_vkQueueSubmit)GetManagedVulkanDeviceProc (targetWindow, instance, device, "vkQueueSubmit");
		if (!vkQueueSubmit) return false;

		VkSubmitInfo submitInfo;
		memset (&submitInfo, 0, sizeof (submitInfo));
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		VkResult result = vkQueueSubmit (queue, 1, &submitInfo, fence);
		if (result != VK_SUCCESS) {

			lastVKError = "vkQueueSubmit failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	HL_PRIM bool HL_NAME(hl_vk_queue_submit) (HL_CFFIPointer* window, int instanceHigh, int instanceLow, int deviceHigh, int deviceLow, int queueHigh,
		int queueLow, int commandBufferHigh, int commandBufferLow, int fenceHigh, int fenceLow) {

#ifdef LIME_VULKAN
		Window* targetWindow = (Window*)window->ptr;
		VkInstance instance = (VkInstance)(uintptr_t)CombineVulkanHandle (instanceHigh, instanceLow);
		VkDevice device = (VkDevice)(uintptr_t)CombineVulkanHandle (deviceHigh, deviceLow);
		VkQueue queue = (VkQueue)(uintptr_t)CombineVulkanHandle (queueHigh, queueLow);
		VkCommandBuffer commandBuffer = (VkCommandBuffer)(uintptr_t)CombineVulkanHandle (commandBufferHigh, commandBufferLow);
		VkFence fence = (VkFence)(uintptr_t)CombineVulkanHandle (fenceHigh, fenceLow);
		PFN_vkQueueSubmit vkQueueSubmit = (PFN_vkQueueSubmit)GetManagedVulkanDeviceProc (targetWindow, instance, device, "vkQueueSubmit");
		if (!vkQueueSubmit) return false;

		VkSubmitInfo submitInfo;
		memset (&submitInfo, 0, sizeof (submitInfo));
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		VkResult result = vkQueueSubmit (queue, 1, &submitInfo, fence);
		if (result != VK_SUCCESS) {

			lastVKError = "vkQueueSubmit failed";
			return false;

		}

		lastVKError.clear ();
		return true;
#else
		lastVKError = "Lime was built without lime-vulkan support";
		return false;
#endif

	}


	value lime_vk_get_last_error () {

		return alloc_string (lastVKError.c_str ());

	}


	HL_PRIM vbyte* HL_NAME(hl_vk_get_last_error) () {

		return hl_copy_bytes ((const vbyte*)lastVKError.c_str (), (int)lastVKError.size () + 1);

	}


#ifdef LIME_VULKAN
	value lime_vulkan_renderer_create (value window, HxString applicationName) {

		Window* targetWindow = (Window*)val_data (window);
		VulkanRenderer* renderer = new VulkanRenderer (targetWindow);

		if (!renderer->Create (applicationName.c_str () ? hxs_utf8 (applicationName, nullptr) : 0)) {

			lastVulkanRendererError = renderer->GetLastError ();
			delete renderer;
			return alloc_null ();

		}

		lastVulkanRendererError.clear ();
		return CFFIPointer (renderer);

	}


	HL_PRIM HL_CFFIPointer* HL_NAME(hl_vulkan_renderer_create) (HL_CFFIPointer* window, hl_vstring* applicationName) {

		Window* targetWindow = (Window*)window->ptr;
		VulkanRenderer* renderer = new VulkanRenderer (targetWindow);
		const char* applicationNameUTF8 = applicationName ? (const char*)hl_to_utf8 ((const uchar*)applicationName->bytes) : 0;

		if (!renderer->Create (applicationNameUTF8)) {

			lastVulkanRendererError = renderer->GetLastError ();
			delete renderer;
			return 0;

		}

		lastVulkanRendererError.clear ();
		return HLCFFIPointer (renderer);

	}


	void lime_vulkan_renderer_destroy (value handle) {

		if (val_is_null (handle)) return;
		VulkanRenderer* renderer = (VulkanRenderer*)val_data (handle);
		delete renderer;

	}


	HL_PRIM void HL_NAME(hl_vulkan_renderer_destroy) (HL_CFFIPointer* handle) {

		if (!handle || !handle->ptr) return;
		VulkanRenderer* renderer = (VulkanRenderer*)handle->ptr;
		handle->ptr = 0;
		delete renderer;

	}


	value lime_vulkan_renderer_get_info (value handle) {

		if (val_is_null (handle)) return alloc_string ("");
		VulkanRenderer* renderer = (VulkanRenderer*)val_data (handle);
		return alloc_string (renderer->GetInfo ().c_str ());

	}


	HL_PRIM vbyte* HL_NAME(hl_vulkan_renderer_get_info) (HL_CFFIPointer* handle) {

		if (!handle || !handle->ptr) return 0;
		VulkanRenderer* renderer = (VulkanRenderer*)handle->ptr;
		return hl_copy_bytes ((const vbyte*)renderer->GetInfo ().c_str (), (int)renderer->GetInfo ().size () + 1);

	}


	value lime_vulkan_renderer_get_last_error () {

		return alloc_string (lastVulkanRendererError.c_str ());

	}


	HL_PRIM vbyte* HL_NAME(hl_vulkan_renderer_get_last_error) () {

		return hl_copy_bytes ((const vbyte*)lastVulkanRendererError.c_str (), (int)lastVulkanRendererError.size () + 1);

	}


	bool lime_vulkan_renderer_set_overlay (value handle, value bytes, int width, int height, int x, int y) {

		if (val_is_null (handle)) return false;
		VulkanRenderer* renderer = (VulkanRenderer*)val_data (handle);
		Bytes data (bytes);
		bool result = renderer->SetOverlay (data.b, width, height, x, y);
		lastVulkanRendererError = renderer->GetLastError ();
		return result;

	}


	HL_PRIM bool HL_NAME(hl_vulkan_renderer_set_overlay) (HL_CFFIPointer* handle, Bytes* bytes, int width, int height, int x, int y) {

		if (!handle || !handle->ptr) return false;
		VulkanRenderer* renderer = (VulkanRenderer*)handle->ptr;
		bool result = renderer->SetOverlay (bytes ? bytes->b : 0, width, height, x, y);
		lastVulkanRendererError = renderer->GetLastError ();
		return result;

	}


	bool lime_vulkan_renderer_clear_overlay (value handle) {

		if (val_is_null (handle)) return false;
		VulkanRenderer* renderer = (VulkanRenderer*)val_data (handle);
		bool result = renderer->ClearOverlay ();
		lastVulkanRendererError = renderer->GetLastError ();
		return result;

	}


	HL_PRIM bool HL_NAME(hl_vulkan_renderer_clear_overlay) (HL_CFFIPointer* handle) {

		if (!handle || !handle->ptr) return false;
		VulkanRenderer* renderer = (VulkanRenderer*)handle->ptr;
		bool result = renderer->ClearOverlay ();
		lastVulkanRendererError = renderer->GetLastError ();
		return result;

	}


	bool lime_vulkan_renderer_render (value handle, double red, double green, double blue, double alpha) {

		if (val_is_null (handle)) return false;
		VulkanRenderer* renderer = (VulkanRenderer*)val_data (handle);
		bool result = renderer->Render (red, green, blue, alpha);
		lastVulkanRendererError = renderer->GetLastError ();
		return result;

	}


	HL_PRIM bool HL_NAME(hl_vulkan_renderer_render) (HL_CFFIPointer* handle, double red, double green, double blue, double alpha) {

		if (!handle || !handle->ptr) return false;
		VulkanRenderer* renderer = (VulkanRenderer*)handle->ptr;
		bool result = renderer->Render (red, green, blue, alpha);
		lastVulkanRendererError = renderer->GetLastError ();
		return result;

	}


	bool lime_vulkan_renderer_resize (value handle) {

		if (val_is_null (handle)) return false;
		VulkanRenderer* renderer = (VulkanRenderer*)val_data (handle);
		bool result = renderer->Resize ();
		lastVulkanRendererError = renderer->GetLastError ();
		return result;

	}


	HL_PRIM bool HL_NAME(hl_vulkan_renderer_resize) (HL_CFFIPointer* handle) {

		if (!handle || !handle->ptr) return false;
		VulkanRenderer* renderer = (VulkanRenderer*)handle->ptr;
		bool result = renderer->Resize ();
		lastVulkanRendererError = renderer->GetLastError ();
		return result;

	}
#else
	value lime_vulkan_renderer_create (value window, HxString applicationName) {

		lastVulkanRendererError = "Lime was built without lime-vulkan support";
		return alloc_null ();

	}


	HL_PRIM HL_CFFIPointer* HL_NAME(hl_vulkan_renderer_create) (HL_CFFIPointer* window, hl_vstring* applicationName) {

		lastVulkanRendererError = "Lime was built without lime-vulkan support";
		return 0;

	}


	void lime_vulkan_renderer_destroy (value handle) {}


	HL_PRIM void HL_NAME(hl_vulkan_renderer_destroy) (HL_CFFIPointer* handle) {}


	value lime_vulkan_renderer_get_info (value handle) {

		return alloc_string ("");

	}


	HL_PRIM vbyte* HL_NAME(hl_vulkan_renderer_get_info) (HL_CFFIPointer* handle) {

		return hl_copy_bytes ((const vbyte*)"", 1);

	}


	value lime_vulkan_renderer_get_last_error () {

		return alloc_string (lastVulkanRendererError.c_str ());

	}


	HL_PRIM vbyte* HL_NAME(hl_vulkan_renderer_get_last_error) () {

		return hl_copy_bytes ((const vbyte*)lastVulkanRendererError.c_str (), (int)lastVulkanRendererError.size () + 1);

	}


	bool lime_vulkan_renderer_set_overlay (value handle, value bytes, int width, int height, int x, int y) {

		lastVulkanRendererError = "Lime was built without lime-vulkan support";
		return false;

	}


	HL_PRIM bool HL_NAME(hl_vulkan_renderer_set_overlay) (HL_CFFIPointer* handle, Bytes* bytes, int width, int height, int x, int y) {

		lastVulkanRendererError = "Lime was built without lime-vulkan support";
		return false;

	}


	bool lime_vulkan_renderer_clear_overlay (value handle) {

		lastVulkanRendererError = "Lime was built without lime-vulkan support";
		return false;

	}


	HL_PRIM bool HL_NAME(hl_vulkan_renderer_clear_overlay) (HL_CFFIPointer* handle) {

		lastVulkanRendererError = "Lime was built without lime-vulkan support";
		return false;

	}


	bool lime_vulkan_renderer_render (value handle, double red, double green, double blue, double alpha) {

		lastVulkanRendererError = "Lime was built without lime-vulkan support";
		return false;

	}


	HL_PRIM bool HL_NAME(hl_vulkan_renderer_render) (HL_CFFIPointer* handle, double red, double green, double blue, double alpha) {

		lastVulkanRendererError = "Lime was built without lime-vulkan support";
		return false;

	}


	bool lime_vulkan_renderer_resize (value handle) {

		lastVulkanRendererError = "Lime was built without lime-vulkan support";
		return false;

	}


	HL_PRIM bool HL_NAME(hl_vulkan_renderer_resize) (HL_CFFIPointer* handle) {

		lastVulkanRendererError = "Lime was built without lime-vulkan support";
		return false;

	}
#endif


	int lime_window_get_display (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->GetDisplay ();

	}


	HL_PRIM int HL_NAME(hl_window_get_display) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->GetDisplay ();

	}


	value lime_window_get_display_mode (value window) {

		Window* targetWindow = (Window*)val_data (window);
		DisplayMode displayMode;
		targetWindow->GetDisplayMode (&displayMode);
		return (value)displayMode.Value ();

	}


	HL_PRIM void HL_NAME(hl_window_get_display_mode) (HL_CFFIPointer* window, DisplayMode* result) {

		Window* targetWindow = (Window*)window->ptr;
		DisplayMode displayMode;
		targetWindow->GetDisplayMode (&displayMode);
		result->CopyFrom(&displayMode);

	}


	int lime_window_get_height (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->GetHeight ();

	}


	HL_PRIM int HL_NAME(hl_window_get_height) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->GetHeight ();

	}


	int32_t lime_window_get_id (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return (int32_t)targetWindow->GetID ();

	}


	HL_PRIM int32_t HL_NAME(hl_window_get_id) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return (int32_t)targetWindow->GetID ();

	}


	bool lime_window_get_mouse_lock (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->GetMouseLock ();

	}


	HL_PRIM bool HL_NAME(hl_window_get_mouse_lock) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->GetMouseLock ();

	}


	double lime_window_get_opacity (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return (float)targetWindow->GetOpacity ();

	}


	HL_PRIM double HL_NAME(hl_window_get_opacity) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return (float)targetWindow->GetOpacity ();

	}


	double lime_window_get_scale (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->GetScale ();

	}


	HL_PRIM double HL_NAME(hl_window_get_scale) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->GetScale ();

	}


	bool lime_window_get_text_input_enabled (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->GetTextInputEnabled ();

	}


	HL_PRIM bool HL_NAME(hl_window_get_text_input_enabled) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->GetTextInputEnabled ();

	}


	int lime_window_get_width (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->GetWidth ();

	}


	HL_PRIM int HL_NAME(hl_window_get_width) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->GetWidth ();

	}


	int lime_window_get_x (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->GetX ();

	}


	HL_PRIM int HL_NAME(hl_window_get_x) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->GetX ();

	}


	int lime_window_get_y (value window) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->GetY ();

	}


	HL_PRIM int HL_NAME(hl_window_get_y) (HL_CFFIPointer* window) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->GetY ();

	}


	void lime_window_move (value window, int x, int y) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->Move (x, y);

	}


	HL_PRIM void HL_NAME(hl_window_move) (HL_CFFIPointer* window, int x, int y) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->Move (x, y);

	}


	value lime_window_read_pixels (value window, value rect, value imageBuffer) {

		Window* targetWindow = (Window*)val_data (window);
		ImageBuffer buffer (imageBuffer);

		if (!val_is_null (rect)) {

			Rectangle _rect = Rectangle (rect);
			targetWindow->ReadPixels (&buffer, &_rect);

		} else {

			targetWindow->ReadPixels (&buffer, NULL);

		}

		return buffer.Value (imageBuffer);

	}


	HL_PRIM ImageBuffer* HL_NAME(hl_window_read_pixels) (HL_CFFIPointer* window, Rectangle* rect, ImageBuffer* imageBuffer) {

		Window* targetWindow = (Window*)window->ptr;

		if (rect) {

			targetWindow->ReadPixels (imageBuffer, rect);

		} else {

			targetWindow->ReadPixels (imageBuffer, NULL);

		}

		return imageBuffer;

	}


	void lime_window_resize (value window, int width, int height) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->Resize (width, height);

	}


	HL_PRIM void HL_NAME(hl_window_resize) (HL_CFFIPointer* window, int width, int height) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->Resize (width, height);

	}


	void lime_window_set_minimum_size (value window, int width, int height) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->SetMinimumSize (width, height);

	}


	HL_PRIM void HL_NAME(hl_window_set_minimum_size) (HL_CFFIPointer* window, int width, int height) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->SetMinimumSize (width, height);

	}


	void lime_window_set_maximum_size (value window, int width, int height) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->SetMaximumSize (width, height);

	}


	HL_PRIM void HL_NAME(hl_window_set_maximum_size) (HL_CFFIPointer* window, int width, int height) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->SetMaximumSize (width, height);

	}


	bool lime_window_set_borderless (value window, bool borderless) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->SetBorderless (borderless);

	}


	HL_PRIM bool HL_NAME(hl_window_set_borderless) (HL_CFFIPointer* window, bool borderless) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->SetBorderless (borderless);

	}


	void lime_window_set_cursor (value window, int cursor) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->SetCursor ((Cursor)cursor);

	}


	HL_PRIM void HL_NAME(hl_window_set_cursor) (HL_CFFIPointer* window, int cursor) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->SetCursor ((Cursor)cursor);

	}


	value lime_window_set_display_mode (value window, value displayMode) {

		Window* targetWindow = (Window*)val_data (window);
		DisplayMode _displayMode (displayMode);
		targetWindow->SetDisplayMode (&_displayMode);
		targetWindow->GetDisplayMode (&_displayMode);
		return (value)_displayMode.Value ();

	}


	HL_PRIM void HL_NAME(hl_window_set_display_mode) (HL_CFFIPointer* window, DisplayMode* displayMode, DisplayMode* result) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->SetDisplayMode (displayMode);
		targetWindow->GetDisplayMode (displayMode);
		result->CopyFrom(displayMode);

	}


	bool lime_window_set_fullscreen (value window, bool fullscreen) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->SetFullscreen (fullscreen);

	}


	HL_PRIM bool HL_NAME(hl_window_set_fullscreen) (HL_CFFIPointer* window, bool fullscreen) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->SetFullscreen (fullscreen);

	}


	void lime_window_set_icon (value window, value buffer) {

		Window* targetWindow = (Window*)val_data (window);
		ImageBuffer imageBuffer = ImageBuffer (buffer);
		targetWindow->SetIcon (&imageBuffer);

	}


	HL_PRIM void HL_NAME(hl_window_set_icon) (HL_CFFIPointer* window, ImageBuffer* buffer) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->SetIcon (buffer);

	}


	bool lime_window_set_maximized (value window, bool maximized) {

		Window* targetWindow = (Window*)val_data(window);
		return targetWindow->SetMaximized (maximized);

	}


	HL_PRIM bool HL_NAME(hl_window_set_maximized) (HL_CFFIPointer* window, bool maximized) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->SetMaximized (maximized);

	}


	bool lime_window_set_minimized (value window, bool minimized) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->SetMinimized (minimized);

	}


	HL_PRIM bool HL_NAME(hl_window_set_minimized) (HL_CFFIPointer* window, bool minimized) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->SetMinimized (minimized);

	}


	void lime_window_set_mouse_lock (value window, bool mouseLock) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->SetMouseLock (mouseLock);

	}


	HL_PRIM void HL_NAME(hl_window_set_mouse_lock) (HL_CFFIPointer* window, bool mouseLock) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->SetMouseLock (mouseLock);

	}


	void lime_window_set_opacity (value window, double opacity) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->SetOpacity ((float)opacity);

	}


	HL_PRIM void HL_NAME(hl_window_set_opacity) (HL_CFFIPointer* window, double opacity) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->SetOpacity ((float)opacity);

	}


	bool lime_window_set_resizable (value window, bool resizable) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->SetResizable (resizable);

	}


	HL_PRIM bool HL_NAME(hl_window_set_resizable) (HL_CFFIPointer* window, bool resizable) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->SetResizable (resizable);

	}


	void lime_window_set_text_input_enabled (value window, bool enabled) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->SetTextInputEnabled (enabled);

	}


	HL_PRIM void HL_NAME(hl_window_set_text_input_enabled) (HL_CFFIPointer* window, bool enabled) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->SetTextInputEnabled (enabled);

	}


	void lime_window_set_text_input_rect (value window, value rect) {

		Window* targetWindow = (Window*)val_data (window);
		Rectangle _rect = Rectangle (rect);
		targetWindow->SetTextInputRect (&_rect);

	}


	HL_PRIM void HL_NAME(hl_window_set_text_input_rect) (HL_CFFIPointer* window, Rectangle* rect) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->SetTextInputRect (rect);

	}


	value lime_window_set_title (value window, HxString title) {

		Window* targetWindow = (Window*)val_data (window);
		const char* titleUtf8 = hxs_utf8 (title, nullptr);
		const char* result = targetWindow->SetTitle (titleUtf8);

		if (result) {

			value _result = alloc_string (result);

			if (result != titleUtf8) {

				free ((char*) result);

			}

			return _result;

		} else {

			return alloc_null ();

		}

	}


	HL_PRIM hl_vstring* HL_NAME(hl_window_set_title) (HL_CFFIPointer* window, hl_vstring* title) {

		Window* targetWindow = (Window*)window->ptr;
		const char* result = targetWindow->SetTitle ((char*)hl_to_utf8 ((const uchar*)title->bytes));

		if (result) {

			return title;

		} else {

			return 0;

		}

	}


	bool lime_window_set_visible (value window, bool visible) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->SetVisible (visible);

	}


	HL_PRIM bool HL_NAME(hl_window_set_visible) (HL_CFFIPointer* window, bool visible) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->SetVisible (visible);

	}


	bool lime_window_set_always_on_top (value window, bool alwaysOnTop) {

		Window* targetWindow = (Window*)val_data (window);
		return targetWindow->SetAlwaysOnTop(alwaysOnTop);

	}


	HL_PRIM bool HL_NAME(hl_window_set_always_on_top) (HL_CFFIPointer* window, bool alwaysOnTop) {

		Window* targetWindow = (Window*)window->ptr;
		return targetWindow->SetAlwaysOnTop (alwaysOnTop);

	}


	void lime_window_warp_mouse (value window, int x, int y) {

		Window* targetWindow = (Window*)val_data (window);
		targetWindow->WarpMouse (x, y);

	}


	HL_PRIM void HL_NAME(hl_window_warp_mouse) (HL_CFFIPointer* window, int x, int y) {

		Window* targetWindow = (Window*)window->ptr;
		targetWindow->WarpMouse (x, y);

	}


	value lime_zlib_compress (value buffer, value bytes) {

		#ifdef LIME_ZLIB
		Bytes data (buffer);
		Bytes result (bytes);

		Zlib::Compress (ZLIB, &data, &result);

		return result.Value (bytes);
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM Bytes* HL_NAME(hl_zlib_compress) (Bytes* buffer, Bytes* bytes) {

		#ifdef LIME_ZLIB
		Zlib::Compress (ZLIB, buffer, bytes);
		return bytes;
		#else
		return 0;
		#endif

	}


	value lime_zlib_decompress (value buffer, value bytes) {

		#ifdef LIME_ZLIB
		Bytes data (buffer);
		Bytes result (bytes);

		Zlib::Decompress (ZLIB, &data, &result);

		return result.Value (bytes);
		#else
		return alloc_null ();
		#endif

	}


	HL_PRIM Bytes* HL_NAME(hl_zlib_decompress) (Bytes* buffer, Bytes* bytes) {

		#ifdef LIME_ZLIB
		Zlib::Decompress (ZLIB, buffer, bytes);
		return bytes;
		#else
		return 0;
		#endif

	}


	DEFINE_PRIME0 (lime_application_create);
	DEFINE_PRIME2v (lime_application_event_manager_register);
	DEFINE_PRIME1 (lime_application_exec);
	DEFINE_PRIME1v (lime_application_init);
	DEFINE_PRIME1 (lime_application_quit);
	DEFINE_PRIME6v (lime_application_set_main_loop);
	DEFINE_PRIME2v (lime_application_set_frame_rate);
	DEFINE_PRIME2v (lime_application_set_vsync_mode);
	DEFINE_PRIME1 (lime_application_update);
	DEFINE_PRIME2 (lime_audio_load);
	DEFINE_PRIME2 (lime_audio_load_bytes);
	DEFINE_PRIME2 (lime_audio_load_file);
	DEFINE_PRIME3 (lime_bytes_from_data_pointer);
	DEFINE_PRIME1 (lime_bytes_get_data_pointer);
	DEFINE_PRIME2 (lime_bytes_get_data_pointer_offset);
	DEFINE_PRIME2 (lime_bytes_read_file);
	DEFINE_PRIME1 (lime_cffi_get_native_pointer);
	DEFINE_PRIME1 (lime_cffi_set_finalizer);
	DEFINE_PRIME2v (lime_clipboard_event_manager_register);
	DEFINE_PRIME0 (lime_clipboard_get_text);
	DEFINE_PRIME1v (lime_clipboard_set_text);
	DEFINE_PRIME2 (lime_data_pointer_offset);
	DEFINE_PRIME2 (lime_deflate_compress);
	DEFINE_PRIME2 (lime_deflate_decompress);
	DEFINE_PRIME2v (lime_drop_event_manager_register);
	DEFINE_PRIME3 (lime_file_dialog_open_directory);
	DEFINE_PRIME3 (lime_file_dialog_open_file);
	DEFINE_PRIME3 (lime_file_dialog_open_files);
	DEFINE_PRIME3 (lime_file_dialog_save_file);
	DEFINE_PRIME1 (lime_file_watcher_create);
	DEFINE_PRIME3 (lime_file_watcher_add_directory);
	DEFINE_PRIME2v (lime_file_watcher_remove_directory);
	DEFINE_PRIME1v (lime_file_watcher_update);
	DEFINE_PRIME1 (lime_font_get_ascender);
	DEFINE_PRIME1 (lime_font_get_descender);
	DEFINE_PRIME1 (lime_font_get_family_name);
	DEFINE_PRIME2 (lime_font_get_glyph_index);
	DEFINE_PRIME2 (lime_font_get_glyph_indices);
	DEFINE_PRIME2 (lime_font_get_glyph_metrics);
	DEFINE_PRIME1 (lime_font_get_height);
	DEFINE_PRIME1 (lime_font_get_num_glyphs);
	DEFINE_PRIME1 (lime_font_get_underline_position);
	DEFINE_PRIME1 (lime_font_get_underline_thickness);
	DEFINE_PRIME1 (lime_font_get_strikethrough_position);
	DEFINE_PRIME1 (lime_font_get_strikethrough_thickness);
	DEFINE_PRIME1 (lime_font_get_units_per_em);
	DEFINE_PRIME1 (lime_font_load);
	DEFINE_PRIME1 (lime_font_load_bytes);
	DEFINE_PRIME1 (lime_font_load_file);
	DEFINE_PRIME2 (lime_font_outline_decompose);
	DEFINE_PRIME2 (lime_font_outline_decompose_no_hint);
	DEFINE_PRIME3 (lime_font_render_glyph);
	DEFINE_PRIME4 (lime_font_render_glyph_with_flags);
	DEFINE_PRIME3 (lime_font_render_glyphs);
	DEFINE_PRIME3v (lime_font_set_size);
	DEFINE_PRIME1v (lime_gamepad_add_mappings);
	DEFINE_PRIME2v (lime_gamepad_event_manager_register);
	DEFINE_PRIME1 (lime_gamepad_get_device_guid);
	DEFINE_PRIME1 (lime_gamepad_get_device_name);
	DEFINE_PRIME4v (lime_gamepad_rumble);
	DEFINE_PRIME2 (lime_gzip_compress);
	DEFINE_PRIME2 (lime_gzip_decompress);
	DEFINE_PRIME2v (lime_haptic_vibrate);
	DEFINE_PRIME3v (lime_image_data_util_color_transform);
	DEFINE_PRIME6v (lime_image_data_util_copy_channel);
	DEFINE_PRIME7v (lime_image_data_util_copy_pixels);
	DEFINE_PRIME4v (lime_image_data_util_fill_rect);
	DEFINE_PRIME5v (lime_image_data_util_flood_fill);
	DEFINE_PRIME4v (lime_image_data_util_get_pixels);
	DEFINE_PRIME8v (lime_image_data_util_merge);
	DEFINE_PRIME1v (lime_image_data_util_multiply_alpha);
	DEFINE_PRIME4v (lime_image_data_util_resize);
	DEFINE_PRIME2v (lime_image_data_util_set_format);
	DEFINE_PRIME6v (lime_image_data_util_set_pixels);
	DEFINE_PRIME12 (lime_image_data_util_threshold);
	DEFINE_PRIME1v (lime_image_data_util_unmultiply_alpha);
	DEFINE_PRIME4 (lime_image_encode);
	DEFINE_PRIME2 (lime_image_load);
	DEFINE_PRIME2 (lime_image_load_bytes);
	DEFINE_PRIME2 (lime_image_load_file);
	DEFINE_PRIME0 (lime_jni_getenv);
	DEFINE_PRIME2v (lime_joystick_event_manager_register);
	DEFINE_PRIME1 (lime_joystick_get_device_guid);
	DEFINE_PRIME1 (lime_joystick_get_device_name);
	DEFINE_PRIME1 (lime_joystick_get_num_axes);
	DEFINE_PRIME1 (lime_joystick_get_num_buttons);
	DEFINE_PRIME1 (lime_joystick_get_num_hats);
	DEFINE_PRIME3 (lime_jpeg_decode_bytes);
	DEFINE_PRIME3 (lime_jpeg_decode_file);
	DEFINE_PRIME1 (lime_key_code_from_scan_code);
	DEFINE_PRIME1 (lime_key_code_to_scan_code);
	DEFINE_PRIME2v (lime_key_event_manager_register);
	DEFINE_PRIME0 (lime_locale_get_system_locale);
	DEFINE_PRIME2 (lime_lzma_compress);
	DEFINE_PRIME2 (lime_lzma_decompress);
	DEFINE_PRIME2v (lime_mouse_event_manager_register);
	DEFINE_PRIME1v (lime_neko_execute);
	DEFINE_PRIME2v (lime_orientation_event_manager_register);
	DEFINE_PRIME3 (lime_png_decode_bytes);
	DEFINE_PRIME3 (lime_png_decode_file);
	DEFINE_PRIME2v (lime_render_event_manager_register);
	DEFINE_PRIME2v (lime_sensor_event_manager_register);
	DEFINE_PRIME0 (lime_system_get_allow_screen_timeout);
	DEFINE_PRIME0 (lime_system_get_device_model);
	DEFINE_PRIME0 (lime_system_get_device_vendor);
	DEFINE_PRIME3 (lime_system_get_directory);
	DEFINE_PRIME1 (lime_system_get_display);
	DEFINE_PRIME0 (lime_system_get_ios_tablet);
	DEFINE_PRIME0 (lime_system_get_num_displays);
	DEFINE_PRIME0 (lime_system_get_device_orientation);
	DEFINE_PRIME0 (lime_system_get_platform_label);
	DEFINE_PRIME0 (lime_system_get_platform_name);
	DEFINE_PRIME0 (lime_system_get_platform_version);
	DEFINE_PRIME0 (lime_system_get_timer);
	DEFINE_PRIME1 (lime_system_get_windows_console_mode);
	DEFINE_PRIME1v (lime_system_open_file);
	DEFINE_PRIME2v (lime_system_open_url);
	DEFINE_PRIME1 (lime_system_set_allow_screen_timeout);
	DEFINE_PRIME2 (lime_system_set_windows_console_mode);
	DEFINE_PRIME2v (lime_text_event_manager_register);
	DEFINE_PRIME2v (lime_touch_event_manager_register);
	DEFINE_PRIME3v (lime_window_alert);
	DEFINE_PRIME1v (lime_window_close);
	DEFINE_PRIME1v (lime_window_context_flip);
	DEFINE_PRIME1 (lime_window_context_lock);
	DEFINE_PRIME1v (lime_window_context_make_current);
	DEFINE_PRIME1v (lime_window_context_unlock);
	DEFINE_PRIME5 (lime_window_create);
	DEFINE_PRIME2v (lime_window_event_manager_register);
	DEFINE_PRIME1v (lime_window_focus);
	DEFINE_PRIME1 (lime_window_get_context);
	DEFINE_PRIME1 (lime_window_get_context_type);
	DEFINE_PRIME3 (lime_window_create_vulkan_surface);
	DEFINE_PRIME1 (lime_window_get_display);
	DEFINE_PRIME1 (lime_window_get_display_mode);
	DEFINE_PRIME1 (lime_window_get_height);
	DEFINE_PRIME1 (lime_window_get_id);
	DEFINE_PRIME1 (lime_window_get_mouse_lock);
	DEFINE_PRIME1 (lime_window_get_scale);
	DEFINE_PRIME1 (lime_window_get_text_input_enabled);
	DEFINE_PRIME1 (lime_window_get_vulkan_drawable_size);
	DEFINE_PRIME1 (lime_window_get_vulkan_instance_extensions);
	DEFINE_PRIME1 (lime_window_get_vulkan_instance_proc_addr);
	DEFINE_PRIME2 (lime_vk_create_instance);
	DEFINE_PRIME3v (lime_vk_destroy_instance);
	DEFINE_PRIME5v (lime_vk_destroy_surface);
	DEFINE_PRIME5 (lime_vk_get_physical_devices);
	DEFINE_PRIME7 (lime_vk_create_device);
	DEFINE_PRIME5v (lime_vk_destroy_device);
	DEFINE_PRIME5 (lime_vk_device_wait_idle);
	DEFINE_PRIME7 (lime_vk_queue_wait_idle);
	DEFINE_PRIME7 (lime_vk_create_command_pool);
	DEFINE_PRIME7v (lime_vk_destroy_command_pool);
	DEFINE_PRIME8 (lime_vk_reset_command_pool);
	DEFINE_PRIME8 (lime_vk_allocate_command_buffer);
	DEFINE_PRIME9v (lime_vk_free_command_buffer);
	DEFINE_PRIME8 (lime_vk_begin_command_buffer);
	DEFINE_PRIME7 (lime_vk_end_command_buffer);
	DEFINE_PRIME8 (lime_vk_reset_command_buffer);
	DEFINE_PRIME5 (lime_vk_create_semaphore);
	DEFINE_PRIME7v (lime_vk_destroy_semaphore);
	DEFINE_PRIME6 (lime_vk_create_fence);
	DEFINE_PRIME7v (lime_vk_destroy_fence);
	DEFINE_PRIME9 (lime_vk_wait_for_fence);
	DEFINE_PRIME7 (lime_vk_reset_fence);
	DEFINE_PRIME11 (lime_vk_queue_submit);
	DEFINE_PRIME0 (lime_vk_get_last_error);
	DEFINE_PRIME2 (lime_vulkan_renderer_create);
	DEFINE_PRIME1v (lime_vulkan_renderer_destroy);
	DEFINE_PRIME1 (lime_vulkan_renderer_get_info);
	DEFINE_PRIME0 (lime_vulkan_renderer_get_last_error);
	DEFINE_PRIME6 (lime_vulkan_renderer_set_overlay);
	DEFINE_PRIME1 (lime_vulkan_renderer_clear_overlay);
	DEFINE_PRIME5 (lime_vulkan_renderer_render);
	DEFINE_PRIME1 (lime_vulkan_renderer_resize);
	DEFINE_PRIME1 (lime_window_get_width);
	DEFINE_PRIME1 (lime_window_get_x);
	DEFINE_PRIME1 (lime_window_get_y);
	DEFINE_PRIME3v (lime_window_move);
	DEFINE_PRIME3 (lime_window_read_pixels);
	DEFINE_PRIME3v (lime_window_resize);
	DEFINE_PRIME3v (lime_window_set_minimum_size);
	DEFINE_PRIME3v (lime_window_set_maximum_size);
	DEFINE_PRIME2 (lime_window_set_borderless);
	DEFINE_PRIME2v (lime_window_set_cursor);
	DEFINE_PRIME2 (lime_window_set_display_mode);
	DEFINE_PRIME2 (lime_window_set_fullscreen);
	DEFINE_PRIME2v (lime_window_set_icon);
	DEFINE_PRIME2 (lime_window_set_maximized);
	DEFINE_PRIME2 (lime_window_set_minimized);
	DEFINE_PRIME2v (lime_window_set_mouse_lock);
	DEFINE_PRIME2 (lime_window_set_resizable);
	DEFINE_PRIME2v (lime_window_set_text_input_enabled);
	DEFINE_PRIME2v (lime_window_set_text_input_rect);
	DEFINE_PRIME2 (lime_window_set_title);
	DEFINE_PRIME2 (lime_window_set_visible);
	DEFINE_PRIME2 (lime_window_set_always_on_top);
	DEFINE_PRIME3v (lime_window_warp_mouse);
	DEFINE_PRIME1 (lime_window_get_opacity);
	DEFINE_PRIME2v (lime_window_set_opacity);
	DEFINE_PRIME2 (lime_zlib_compress);
	DEFINE_PRIME2 (lime_zlib_decompress);


	#define _ENUM "?"
	// #define _TCFFIPOINTER _ABSTRACT (HL_CFFIPointer)
	#define _TAPPLICATION_EVENT _OBJ (_I32 _I32)
	#define _TBYTES _OBJ (_I32 _BYTES)
	#define _TCFFIPOINTER _DYN
	#define _TCLIPBOARD_EVENT _OBJ (_I32)
	#define _TDISPLAYMODE _OBJ (_I32 _I32 _I32 _I32)
	#define _TDROP_EVENT _OBJ (_BYTES _I32)
	#define _TGAMEPAD_EVENT _OBJ (_I32 _I32 _I32 _I32 _F64)
	#define _TJOYSTICK_EVENT _OBJ (_I32 _I32 _I32 _I32 _F64 _F64)
	#define _TKEY_EVENT _OBJ (_F64 _I32 _I32 _I32)
	#define _TMOUSE_EVENT _OBJ (_I32 _F64 _F64 _I32 _I32 _F64 _F64 _I32)
	#define _TORIENTATION_EVENT _OBJ (_I32 _I32 _I32)
	#define _TRECTANGLE _OBJ (_F64 _F64 _F64 _F64)
	#define _TRENDER_EVENT _OBJ (_I32)
	#define _TSENSOR_EVENT _OBJ (_I32 _F64 _F64 _F64 _I32)
	#define _TTEXT_EVENT _OBJ (_I32 _I32 _I32 _BYTES _I32 _I32)
	#define _TTOUCH_EVENT _OBJ (_I32 _F64 _F64 _I32 _F64 _I32 _F64 _F64)
	#define _TVECTOR2 _OBJ (_F64 _F64)
	#define _TVORBISFILE _OBJ (_I32 _DYN)
	#define _TWINDOW_EVENT _OBJ (_I32 _I32 _I32 _I32 _I32 _I32)

	#define _TARRAYBUFFER _TBYTES
	#define _TARRAYBUFFERVIEW _OBJ (_I32 _TARRAYBUFFER _I32 _I32 _I32 _I32)
	#define _TAUDIOBUFFER _OBJ (_I32 _I32 _TARRAYBUFFERVIEW _I32 _DYN _DYN _DYN _DYN _BOOL _DYN _TBYTES _BOOL _I32 _STRING _TBYTES _STRING _TVORBISFILE)
	#define _TIMAGEBUFFER _OBJ (_I32 _TARRAYBUFFERVIEW _I32 _I32 _BOOL _BOOL _I32 _DYN _DYN _DYN _DYN _DYN _DYN)
	#define _TIMAGE _OBJ (_TIMAGEBUFFER _BOOL _I32 _I32 _I32 _TRECTANGLE _ENUM _I32 _I32 _F64 _F64)

	#define _TARRAY _OBJ (_BYTES _I32)
	#define _TARRAY2 _OBJ (_ARR)


	DEFINE_HL_PRIM (_TCFFIPOINTER, hl_application_create, _NO_ARG);
	DEFINE_HL_PRIM (_VOID, hl_application_event_manager_register, _FUN(_VOID, _NO_ARG) _TAPPLICATION_EVENT);
	DEFINE_HL_PRIM (_I32, hl_application_exec, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_VOID, hl_application_init, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_application_quit, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_VOID, hl_application_set_main_loop, _TCFFIPOINTER _I32 _F64 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_application_set_frame_rate, _TCFFIPOINTER _F64);
	DEFINE_HL_PRIM (_VOID, hl_application_set_vsync_mode, _TCFFIPOINTER _I32);
	DEFINE_HL_PRIM (_BOOL, hl_application_update, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_TAUDIOBUFFER, hl_audio_load_bytes, _TBYTES _TAUDIOBUFFER);
	DEFINE_HL_PRIM (_TAUDIOBUFFER, hl_audio_load_file, _STRING _TAUDIOBUFFER);
	DEFINE_HL_PRIM (_TBYTES, hl_bytes_from_data_pointer, _F64 _I32 _TBYTES);
	DEFINE_HL_PRIM (_F64, hl_bytes_get_data_pointer, _TBYTES);
	DEFINE_HL_PRIM (_F64, hl_bytes_get_data_pointer_offset, _TBYTES _I32);
	DEFINE_HL_PRIM (_TBYTES, hl_bytes_read_file, _STRING _TBYTES);
	DEFINE_HL_PRIM (_F64, hl_cffi_get_native_pointer, _TCFFIPOINTER);
	// DEFINE_PRIME1 (lime_cffi_set_finalizer);
	DEFINE_HL_PRIM (_VOID, hl_clipboard_event_manager_register, _FUN(_VOID, _NO_ARG) _TCLIPBOARD_EVENT);
	DEFINE_HL_PRIM (_BYTES, hl_clipboard_get_text, _NO_ARG);
	DEFINE_HL_PRIM (_VOID, hl_clipboard_set_text, _STRING);
	DEFINE_HL_PRIM (_F64, hl_data_pointer_offset, _F64 _I32);
	DEFINE_HL_PRIM (_TBYTES, hl_deflate_compress, _TBYTES _TBYTES);
	DEFINE_HL_PRIM (_TBYTES, hl_deflate_decompress, _TBYTES _TBYTES);
	DEFINE_HL_PRIM (_VOID, hl_drop_event_manager_register, _FUN(_VOID, _NO_ARG) _TDROP_EVENT);
	DEFINE_HL_PRIM (_BYTES, hl_file_dialog_open_directory, _STRING _STRING _STRING);
	DEFINE_HL_PRIM (_BYTES, hl_file_dialog_open_file, _STRING _STRING _STRING);
	DEFINE_HL_PRIM (_ARR, hl_file_dialog_open_files, _STRING _STRING _STRING);
	DEFINE_HL_PRIM (_BYTES, hl_file_dialog_save_file, _STRING _STRING _STRING);
	DEFINE_HL_PRIM (_TCFFIPOINTER, hl_file_watcher_create, _DYN);
	DEFINE_HL_PRIM (_I32, hl_file_watcher_add_directory, _TCFFIPOINTER _STRING _BOOL);
	DEFINE_HL_PRIM (_VOID, hl_file_watcher_remove_directory, _TCFFIPOINTER _I32);
	DEFINE_HL_PRIM (_VOID, hl_file_watcher_update, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_font_get_ascender, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_font_get_descender, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_BYTES, hl_font_get_family_name, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_font_get_glyph_index, _TCFFIPOINTER _STRING);
	DEFINE_HL_PRIM (_ARR, hl_font_get_glyph_indices, _TCFFIPOINTER _STRING);
	DEFINE_HL_PRIM (_DYN, hl_font_get_glyph_metrics, _TCFFIPOINTER _I32);
	DEFINE_HL_PRIM (_I32, hl_font_get_height, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_font_get_num_glyphs, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_font_get_underline_position, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_font_get_underline_thickness, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_font_get_strikethrough_position, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_font_get_strikethrough_thickness, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_font_get_units_per_em, _TCFFIPOINTER);
	// DEFINE_PRIME1 (lime_font_load);
	DEFINE_HL_PRIM (_TCFFIPOINTER, hl_font_load_bytes, _TBYTES);
	DEFINE_HL_PRIM (_TCFFIPOINTER, hl_font_load_file, _STRING);
	DEFINE_HL_PRIM (_DYN, hl_font_outline_decompose, _TCFFIPOINTER _I32);
	DEFINE_HL_PRIM (_DYN, hl_font_outline_decompose_no_hint, _TCFFIPOINTER _I32);
	DEFINE_HL_PRIM (_TBYTES, hl_font_render_glyph, _TCFFIPOINTER _I32 _TBYTES);
	DEFINE_HL_PRIM (_TBYTES, hl_font_render_glyph_with_flags, _TCFFIPOINTER _I32 _I32 _TBYTES);
	DEFINE_HL_PRIM (_TBYTES, hl_font_render_glyphs, _TCFFIPOINTER _ARR _TBYTES);
	DEFINE_HL_PRIM (_VOID, hl_font_set_size, _TCFFIPOINTER _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_gamepad_add_mappings, _ARR);
	DEFINE_HL_PRIM (_VOID, hl_gamepad_event_manager_register, _FUN(_VOID, _NO_ARG) _TGAMEPAD_EVENT);
	DEFINE_HL_PRIM (_BYTES, hl_gamepad_get_device_guid, _I32);
	DEFINE_HL_PRIM (_BYTES, hl_gamepad_get_device_name, _I32);
	DEFINE_HL_PRIM (_VOID, hl_gamepad_rumble, _I32 _F64 _F64 _I32);
	DEFINE_HL_PRIM (_TBYTES, hl_gzip_compress, _TBYTES _TBYTES);
	DEFINE_HL_PRIM (_TBYTES, hl_gzip_decompress, _TBYTES _TBYTES);
	DEFINE_HL_PRIM (_VOID, hl_haptic_vibrate, _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_color_transform, _TIMAGE _TRECTANGLE _TARRAYBUFFERVIEW);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_copy_channel, _TIMAGE _TIMAGE _TRECTANGLE _TVECTOR2 _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_copy_pixels, _TIMAGE _TIMAGE _TRECTANGLE _TVECTOR2 _TIMAGE _TVECTOR2 _BOOL);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_fill_rect, _TIMAGE _TRECTANGLE _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_flood_fill, _TIMAGE _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_get_pixels, _TIMAGE _TRECTANGLE _I32 _TBYTES);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_merge, _TIMAGE _TIMAGE _TRECTANGLE _TVECTOR2 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_multiply_alpha, _TIMAGE);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_resize, _TIMAGE _TIMAGEBUFFER _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_set_format, _TIMAGE _I32);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_set_pixels, _TIMAGE _TRECTANGLE _TBYTES _I32 _I32 _I32);
	DEFINE_HL_PRIM (_I32, hl_image_data_util_threshold, _TIMAGE _TIMAGE _TRECTANGLE _TVECTOR2 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _BOOL);
	DEFINE_HL_PRIM (_VOID, hl_image_data_util_unmultiply_alpha, _TIMAGE);
	DEFINE_HL_PRIM (_TBYTES, hl_image_encode, _TIMAGEBUFFER _I32 _I32 _TBYTES);
	// DEFINE_PRIME2 (lime_image_load);
	DEFINE_HL_PRIM (_TIMAGEBUFFER, hl_image_load_bytes, _TBYTES _TIMAGEBUFFER);
	DEFINE_HL_PRIM (_TIMAGEBUFFER, hl_image_load_file, _STRING _TIMAGEBUFFER);
	DEFINE_HL_PRIM (_F64, hl_jni_getenv, _NO_ARG);
	DEFINE_HL_PRIM (_VOID, hl_joystick_event_manager_register, _FUN(_VOID, _NO_ARG) _TJOYSTICK_EVENT);
	DEFINE_HL_PRIM (_BYTES, hl_joystick_get_device_guid, _I32);
	DEFINE_HL_PRIM (_BYTES, hl_joystick_get_device_name, _I32);
	DEFINE_HL_PRIM (_I32, hl_joystick_get_num_axes, _I32);
	DEFINE_HL_PRIM (_I32, hl_joystick_get_num_buttons, _I32);
	DEFINE_HL_PRIM (_I32, hl_joystick_get_num_hats, _I32);
	DEFINE_HL_PRIM (_TIMAGEBUFFER, hl_jpeg_decode_bytes, _TBYTES _BOOL _TIMAGEBUFFER);
	DEFINE_HL_PRIM (_TIMAGEBUFFER, hl_jpeg_decode_file, _STRING _BOOL _TIMAGEBUFFER);
	DEFINE_HL_PRIM (_I32, hl_key_code_from_scan_code, _I32);
	DEFINE_HL_PRIM (_I32, hl_key_code_to_scan_code, _I32);
	DEFINE_HL_PRIM (_VOID, hl_key_event_manager_register, _FUN (_VOID, _NO_ARG) _TKEY_EVENT);
	DEFINE_HL_PRIM (_BYTES, hl_locale_get_system_locale, _NO_ARG);
	DEFINE_HL_PRIM (_TBYTES, hl_lzma_compress, _TBYTES _TBYTES);
	DEFINE_HL_PRIM (_TBYTES, hl_lzma_decompress, _TBYTES _TBYTES);
	DEFINE_HL_PRIM (_VOID, hl_mouse_event_manager_register, _FUN (_VOID, _NO_ARG) _TMOUSE_EVENT);
	// DEFINE_PRIME1v (lime_neko_execute);
	DEFINE_HL_PRIM (_VOID, hl_orientation_event_manager_register, _FUN (_VOID, _NO_ARG) _TORIENTATION_EVENT);
	DEFINE_HL_PRIM (_TIMAGEBUFFER, hl_png_decode_bytes, _TBYTES _BOOL _TIMAGEBUFFER);
	DEFINE_HL_PRIM (_TIMAGEBUFFER, hl_png_decode_file, _STRING _BOOL _TIMAGEBUFFER);
	DEFINE_HL_PRIM (_VOID, hl_render_event_manager_register, _FUN (_VOID, _NO_ARG) _TRENDER_EVENT);
	DEFINE_HL_PRIM (_VOID, hl_sensor_event_manager_register, _FUN (_VOID, _NO_ARG) _TSENSOR_EVENT);
	DEFINE_HL_PRIM (_BOOL, hl_system_get_allow_screen_timeout, _NO_ARG);
	DEFINE_HL_PRIM (_BYTES, hl_system_get_device_model, _NO_ARG);
	DEFINE_HL_PRIM (_BYTES, hl_system_get_device_vendor, _NO_ARG);
	DEFINE_HL_PRIM (_BYTES, hl_system_get_directory, _I32 _STRING _STRING);
	DEFINE_HL_PRIM (_DYN, hl_system_get_display, _I32);
	DEFINE_HL_PRIM (_BOOL, hl_system_get_ios_tablet, _NO_ARG);
	DEFINE_HL_PRIM (_I32, hl_system_get_num_displays, _NO_ARG);
	DEFINE_HL_PRIM (_I32, hl_system_get_device_orientation, _NO_ARG);
	DEFINE_HL_PRIM (_BYTES, hl_system_get_platform_label, _NO_ARG);
	DEFINE_HL_PRIM (_BYTES, hl_system_get_platform_name, _NO_ARG);
	DEFINE_HL_PRIM (_BYTES, hl_system_get_platform_version, _NO_ARG);
	DEFINE_HL_PRIM (_F64, hl_system_get_timer, _NO_ARG);
	DEFINE_HL_PRIM (_I32, hl_system_get_windows_console_mode, _I32);
	DEFINE_HL_PRIM (_VOID, hl_system_open_file, _STRING);
	DEFINE_HL_PRIM (_VOID, hl_system_open_url, _STRING _STRING);
	DEFINE_HL_PRIM (_BOOL, hl_system_set_allow_screen_timeout, _BOOL);
	DEFINE_HL_PRIM (_BOOL, hl_system_set_windows_console_mode, _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_text_event_manager_register, _FUN (_VOID, _NO_ARG) _TTEXT_EVENT);
	DEFINE_HL_PRIM (_VOID, hl_touch_event_manager_register, _FUN (_VOID, _NO_ARG) _TTOUCH_EVENT);
	DEFINE_HL_PRIM (_VOID, hl_window_alert, _TCFFIPOINTER _STRING _STRING);
	DEFINE_HL_PRIM (_VOID, hl_window_close, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_VOID, hl_window_context_flip, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_DYN, hl_window_context_lock, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_VOID, hl_window_context_make_current, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_VOID, hl_window_context_unlock, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_TCFFIPOINTER, hl_window_create, _TCFFIPOINTER _I32 _I32 _I32 _STRING);
	DEFINE_HL_PRIM (_VOID, hl_window_event_manager_register, _FUN (_VOID, _NO_ARG) _TWINDOW_EVENT);
	DEFINE_HL_PRIM (_VOID, hl_window_focus, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_F64, hl_window_get_context, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_BYTES, hl_window_get_context_type, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_DYN, hl_window_create_vulkan_surface, _TCFFIPOINTER _I32 _I32);
	DEFINE_HL_PRIM (_I32, hl_window_get_display, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_VOID, hl_window_get_display_mode, _TCFFIPOINTER _TDISPLAYMODE);
	DEFINE_HL_PRIM (_I32, hl_window_get_height, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_window_get_id, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_BOOL, hl_window_get_mouse_lock, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_F64, hl_window_get_scale, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_BOOL, hl_window_get_text_input_enabled, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_DYN, hl_window_get_vulkan_drawable_size, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_ARR, hl_window_get_vulkan_instance_extensions, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_DYN, hl_window_get_vulkan_instance_proc_addr, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_DYN, hl_vk_create_instance, _TCFFIPOINTER _STRING);
	DEFINE_HL_PRIM (_VOID, hl_vk_destroy_instance, _TCFFIPOINTER _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_vk_destroy_surface, _TCFFIPOINTER _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_ARR, hl_vk_get_physical_devices, _TCFFIPOINTER _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_DYN, hl_vk_create_device, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _ARR);
	DEFINE_HL_PRIM (_VOID, hl_vk_destroy_device, _TCFFIPOINTER _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_vk_device_wait_idle, _TCFFIPOINTER _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_vk_queue_wait_idle, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_DYN, hl_vk_create_command_pool, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_vk_destroy_command_pool, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_vk_reset_command_pool, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_DYN, hl_vk_allocate_command_buffer, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_vk_free_command_buffer, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_vk_begin_command_buffer, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_vk_end_command_buffer, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_vk_reset_command_buffer, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_DYN, hl_vk_create_semaphore, _TCFFIPOINTER _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_vk_destroy_semaphore, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_DYN, hl_vk_create_fence, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_vk_destroy_fence, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_vk_wait_for_fence, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_vk_reset_fence, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_vk_queue_submit, _TCFFIPOINTER _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BYTES, hl_vk_get_last_error, _NO_ARG);
	DEFINE_HL_PRIM (_TCFFIPOINTER, hl_vulkan_renderer_create, _TCFFIPOINTER _STRING);
	DEFINE_HL_PRIM (_VOID, hl_vulkan_renderer_destroy, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_BYTES, hl_vulkan_renderer_get_info, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_BYTES, hl_vulkan_renderer_get_last_error, _NO_ARG);
	DEFINE_HL_PRIM (_BOOL, hl_vulkan_renderer_set_overlay, _TCFFIPOINTER _TBYTES _I32 _I32 _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_vulkan_renderer_clear_overlay, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_BOOL, hl_vulkan_renderer_render, _TCFFIPOINTER _F64 _F64 _F64 _F64);
	DEFINE_HL_PRIM (_BOOL, hl_vulkan_renderer_resize, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_window_get_width, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_window_get_x, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_I32, hl_window_get_y, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_VOID, hl_window_move, _TCFFIPOINTER _I32 _I32);
	DEFINE_HL_PRIM (_DYN, hl_window_read_pixels, _TCFFIPOINTER _TRECTANGLE _TIMAGEBUFFER);
	DEFINE_HL_PRIM (_VOID, hl_window_resize, _TCFFIPOINTER _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_window_set_minimum_size, _TCFFIPOINTER _I32 _I32);
	DEFINE_HL_PRIM (_VOID, hl_window_set_maximum_size, _TCFFIPOINTER _I32 _I32);
	DEFINE_HL_PRIM (_BOOL, hl_window_set_borderless, _TCFFIPOINTER _BOOL);
	DEFINE_HL_PRIM (_VOID, hl_window_set_cursor, _TCFFIPOINTER _I32);
	DEFINE_HL_PRIM (_VOID, hl_window_set_display_mode, _TCFFIPOINTER _TDISPLAYMODE _TDISPLAYMODE);
	DEFINE_HL_PRIM (_BOOL, hl_window_set_fullscreen, _TCFFIPOINTER _BOOL);
	DEFINE_HL_PRIM (_VOID, hl_window_set_icon, _TCFFIPOINTER _TIMAGEBUFFER);
	DEFINE_HL_PRIM (_BOOL, hl_window_set_maximized, _TCFFIPOINTER _BOOL);
	DEFINE_HL_PRIM (_BOOL, hl_window_set_minimized, _TCFFIPOINTER _BOOL);
	DEFINE_HL_PRIM (_VOID, hl_window_set_mouse_lock, _TCFFIPOINTER _BOOL);
	DEFINE_HL_PRIM (_BOOL, hl_window_set_resizable, _TCFFIPOINTER _BOOL);
	DEFINE_HL_PRIM (_VOID, hl_window_set_text_input_enabled, _TCFFIPOINTER _BOOL);
	DEFINE_HL_PRIM (_VOID, hl_window_set_text_input_rect, _TCFFIPOINTER _TRECTANGLE);
	DEFINE_HL_PRIM (_STRING, hl_window_set_title, _TCFFIPOINTER _STRING);
	DEFINE_HL_PRIM (_BOOL, hl_window_set_visible, _TCFFIPOINTER _BOOL);
	DEFINE_HL_PRIM (_BOOL, hl_window_set_always_on_top, _TCFFIPOINTER _BOOL);
	DEFINE_HL_PRIM (_VOID, hl_window_warp_mouse, _TCFFIPOINTER _I32 _I32);
	DEFINE_HL_PRIM (_F64, hl_window_get_opacity, _TCFFIPOINTER);
	DEFINE_HL_PRIM (_VOID, hl_window_set_opacity, _TCFFIPOINTER _F64);
	DEFINE_HL_PRIM (_TBYTES, hl_zlib_compress, _TBYTES _TBYTES);
	DEFINE_HL_PRIM (_TBYTES, hl_zlib_decompress, _TBYTES _TBYTES);


}


#ifdef LIME_CAIRO
extern "C" int lime_cairo_register_prims ();
#else
extern "C" int lime_cairo_register_prims () { return 0; }
#endif

#ifdef LIME_CURL
extern "C" int lime_curl_register_prims ();
#else
extern "C" int lime_curl_register_prims () { return 0; }
#endif

#ifdef LIME_HARFBUZZ
extern "C" int lime_harfbuzz_register_prims ();
#else
extern "C" int lime_harfbuzz_register_prims () { return 0; }
#endif

#ifdef LIME_OPENAL
extern "C" int lime_openal_register_prims ();
#else
extern "C" int lime_openal_register_prims () { return 0; }
#endif

#ifdef LIME_OPENGL
extern "C" int lime_opengl_register_prims ();
#else
extern "C" int lime_opengl_register_prims () { return 0; }
#endif

#ifdef LIME_VORBIS
extern "C" int lime_vorbis_register_prims ();
#else
extern "C" int lime_vorbis_register_prims () { return 0; }
#endif


extern "C" int lime_register_prims () {

	lime_cairo_register_prims ();
	lime_curl_register_prims ();
	lime_harfbuzz_register_prims ();
	lime_openal_register_prims ();
	lime_opengl_register_prims ();
	lime_vorbis_register_prims ();

	return 0;

}
