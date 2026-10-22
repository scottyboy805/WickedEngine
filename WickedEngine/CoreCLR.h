
#define NETHOST_USE_AS_STATIC

#include <string>
#include "WickedEngine.h"
#include "hostfxr.h"
#include "nethost.h"
#include "coreclr_delegates.h"

namespace CoreCLR
{
	struct Host
	{
	private:
		const std::string runtimeConfigName = "Kora.runtimeconfig.json";

		bool initialized = false;

		hostfxr_handle fxrHandle = nullptr;
		hostfxr_initialize_for_runtime_config_fn fxrInit = nullptr;
		hostfxr_get_runtime_delegate_fn fxrGetDelegate = nullptr;
		hostfxr_close_fn fxrClose = nullptr;

		get_function_pointer_fn clrGetFunctionPointer = nullptr;
		load_assembly_fn clrLoadAssembly = nullptr;
		load_assembly_bytes_fn clrLoadAssemblyBytes = nullptr;

		friend int Initialize();
		friend int GetFunctionPointer(const std::string typeName, const std::string methodName, void** delegate);
		friend int LoadAssembly(const std::string assemblyPath);
		friend void Shutdown();

	public:
		std::string hostFxrPath = "";
		HMODULE hostLib = nullptr;

		bool IsInitialized() const noexcept
		{
			return initialized;
		}
	};

	Host* GetHost();

	int Initialize();
	int GetFunctionPointer(const std::string typeName, const std::string methodName, void** delegate);
	int LoadAssembly(const std::string assemblyPath);
	void Shutdown();
}
