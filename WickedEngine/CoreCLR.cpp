
#include "CoreCLR.h"

#define MAX_PATH 260

namespace CoreCLR
{
	static Host host;

	Host* GetHost()
	{
		return &host;
	}

	int Initialize()
	{
		// Get hostxr path
		char_t buffer[MAX_PATH];
		size_t buffer_size = sizeof(buffer) / sizeof(char_t);
		int rc = get_hostfxr_path(buffer, &buffer_size, nullptr);
		if (rc != 0)
		{
			wi::backlog::post("Failed to get hostfxr path! Error code: " + std::to_string(rc));
			return rc;
		}

		// Create string
		std::wstring hostPath(buffer, buffer_size);
		host.hostFxrPath.assign(hostPath.begin(), hostPath.end());

		// Report path
		wi::backlog::post("hostfxr path: " + host.hostFxrPath);

		// Load hostfxr
		host.hostLib = wiLoadLibrary(host.hostFxrPath.c_str());

		if (host.hostLib == nullptr)
		{
			wi::backlog::post("Failed to load hostfxr library!");
			return -1;
		}

		// Get hostfxr functions
		host.fxrInit = (hostfxr_initialize_for_runtime_config_fn)wiGetProcAddress(host.hostLib, "hostfxr_initialize_for_runtime_config");
		host.fxrGetDelegate = (hostfxr_get_runtime_delegate_fn)wiGetProcAddress(host.hostLib, "hostfxr_get_runtime_delegate");
		host.fxrClose = (hostfxr_close_fn)wiGetProcAddress(host.hostLib, "hostfxr_close");

		if (host.fxrInit == nullptr || host.fxrGetDelegate == nullptr || host.fxrClose == nullptr)
		{
			wi::backlog::post("Failed to get hostfxr functions!");
			return -1;
		}

		// Get config path
		std::string runtimeConfigPath(host.runtimeConfigName.begin(), host.runtimeConfigName.end());
		wi::helper::MakePathAbsolute(runtimeConfigPath);

		// Check exists
		if(wi::helper::FileExists(runtimeConfigPath) == false)
		{
			wi::backlog::post("Runtime config file does not exist: " + runtimeConfigPath);
			return -1;
		}

		// Initialize hostfxr
		std::wstring wruntimeConfigPath(runtimeConfigPath.begin(), runtimeConfigPath.end());
		rc = host.fxrInit(wruntimeConfigPath.c_str(), nullptr, &host.fxrHandle);

		// Check for hostfxr error
		if (rc != 0 || host.fxrHandle == nullptr)
		{
			wi::backlog::post("Failed to initialize hostfxr! Error code: " + std::to_string(rc));
			return rc;
		}

		// Get function pointer
		rc = host.fxrGetDelegate(host.fxrHandle, hdt_get_function_pointer, (void**)&host.clrGetFunctionPointer);
		if (rc != 0)
		{
			wi::backlog::post("Failed to get get_function_pointer delegate! Error code: " + std::to_string(rc));
			return rc;
		}

		// Load assembly
		rc = host.fxrGetDelegate(host.fxrHandle, hdt_load_assembly, (void**)&host.clrLoadAssembly);
		if (rc != 0)
		{
			wi::backlog::post("Failed to get load_assembly delegate! Error code: " + std::to_string(rc));
			return rc;
		}

		// Load assembly bytes
		rc = host.fxrGetDelegate(host.fxrHandle, hdt_load_assembly_bytes, (void**)&host.clrLoadAssemblyBytes);
		if (rc != 0)
		{
			wi::backlog::post("Failed to get load_assembly_bytes delegate! Error code: " + std::to_string(rc));
			return rc;
		}

		// Set init
		host.initialized = true;
		return 0;
	}

	int LoadAssembly(const std::string assemblyPath)
	{
		if (!host.IsInitialized())
		{
			wi::backlog::post("CoreCLR is not initialized!");
			return -1;
		}
		std::wstring wassemblyPath(assemblyPath.begin(), assemblyPath.end());
		int rc = host.clrLoadAssembly(wassemblyPath.c_str(), nullptr, nullptr);
		if (rc != 0)
		{
			wi::backlog::post("Failed to load assembly: " + assemblyPath + " Error code: " + std::to_string(rc));
			return rc;
		}
		return 0;
	}

	int GetFunctionPointer(const std::string typeName, const std::string methodName, void** delegate)
	{
		if (!host.IsInitialized())
		{
			wi::backlog::post("CoreCLR is not initialized!");
			return -1;
		}
		std::wstring wtypeName(typeName.begin(), typeName.end());
		std::wstring wmethodName(methodName.begin(), methodName.end());
		int rc = host.clrGetFunctionPointer(wtypeName.c_str(), wmethodName.c_str(), nullptr, nullptr, nullptr, delegate);
		if (rc != 0)
		{
			wi::backlog::post("Failed to get function pointer for: " + typeName + "." + methodName + " Error code: " + std::to_string(rc));
			return rc;
		}
		return 0;
	}

	void Shutdown()
	{
		host.clrGetFunctionPointer = nullptr;
		host.clrLoadAssembly = nullptr;
		host.clrLoadAssemblyBytes = nullptr;

		if (host.fxrHandle)
		{
			host.fxrClose(host.fxrHandle);
			host.fxrHandle = nullptr;
		}

		if (host.hostLib)
		{
			wiFreeLibrary(host.hostLib);
			host.hostLib = nullptr;
		}
		host.hostFxrPath.clear();
		host.initialized = false;
	}
}
