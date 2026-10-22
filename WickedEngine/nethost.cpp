#ifndef NETHOST_EXPORT
#define NETHOST_USE_AS_STATIC
#endif
#include "nethost.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <link.h>
#endif
#include <sys/stat.h>
#endif

namespace
{
	namespace fs = std::filesystem;
	using string_t = std::basic_string<char_t>;

	constexpr int success = 0;
	constexpr int invalid_arg = static_cast<int>(0x80008081u);
	constexpr int hostfxr_missing = static_cast<int>(0x80008083u);
	constexpr int buffer_too_small = static_cast<int>(0x80008098u);

#ifdef _WIN32
	constexpr char_t slash = L'\\';
	constexpr const wchar_t* hostfxr_name = L"hostfxr.dll";
#elif defined(__APPLE__)
	constexpr char_t slash = '/';
	constexpr const char* hostfxr_name = "libhostfxr.dylib";
#else
	constexpr char_t slash = '/';
	constexpr const char* hostfxr_name = "libhostfxr.so";
#endif

	static bool path_is_directory(const fs::path& path)
	{
		std::error_code error;
		return fs::is_directory(path, error);
	}

	static bool path_is_file(const fs::path& path)
	{
		std::error_code error;
		return fs::is_regular_file(path, error);
	}

	static bool version_is_greater(const std::string& left, const std::string& right)
	{
		auto split = [](const std::string& value)
			{
				std::vector<uint64_t> numbers;
				size_t start = 0;
				while (start < value.size())
				{
					size_t end = value.find('.', start);
					size_t delimiter = end == std::string::npos ? value.size() : end;
					size_t suffix = value.find_first_of("-+", start);
					if (suffix != std::string::npos && suffix < delimiter)
						delimiter = suffix;
					uint64_t number = 0;
					for (size_t i = start; i < delimiter && value[i] >= '0' && value[i] <= '9'; ++i)
						number = number * 10 + static_cast<uint64_t>(value[i] - '0');
					numbers.push_back(number);
					if (end == std::string::npos)
						break;
					start = end + 1;
				}
				return numbers;
			};

		std::vector<uint64_t> l = split(left);
		std::vector<uint64_t> r = split(right);
		size_t count = (std::max)(l.size(), r.size());
		for (size_t i = 0; i < count; ++i)
		{
			uint64_t lv = i < l.size() ? l[i] : 0;
			uint64_t rv = i < r.size() ? r[i] : 0;
			if (lv != rv)
				return lv > rv;
		}

		bool left_prerelease = left.find('-') != std::string::npos;
		bool right_prerelease = right.find('-') != std::string::npos;
		return right_prerelease && !left_prerelease;
	}

	static fs::path hostfxr_in(const fs::path& root)
	{
		fs::path directory = root / "host" / "fxr";
		if (!path_is_directory(directory))
			return {};

		fs::path best;
		std::error_code error;
		for (const fs::directory_entry& entry : fs::directory_iterator(directory, error))
		{
			if (error || !entry.is_directory(error))
				continue;

			fs::path candidate = entry.path() / hostfxr_name;
			if (!path_is_file(candidate))
				continue;

			if (best.empty() || version_is_greater(entry.path().filename().string(), best.filename().string()))
				best = entry.path();
		}

		return best.empty() ? fs::path{} : best / hostfxr_name;
	}

	static string_t environment(const char_t* name)
	{
#ifdef _WIN32
		DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
		if (required == 0)
			return {};
		string_t result(required, L'\0');
		DWORD length = GetEnvironmentVariableW(name, result.data(), required);
		if (length == 0)
			return {};
		result.resize(length);
		return result;
#else
		const char* value = std::getenv(name);
		return value == nullptr ? string_t{} : string_t(value);
#endif
	}

#ifdef _WIN32
	static void add_registry_root(std::vector<fs::path>& roots, const wchar_t* architecture)
	{
		using reg_open_key_ex = LONG(WINAPI*)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
		using reg_query_value = LONG(WINAPI*)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
		using reg_close_key = LONG(WINAPI*)(HKEY);

		HMODULE advapi = LoadLibraryW(L"advapi32.dll");
		if (advapi == nullptr)
			return;

		auto open = reinterpret_cast<reg_open_key_ex>(GetProcAddress(advapi, "RegOpenKeyExW"));
		auto query = reinterpret_cast<reg_query_value>(GetProcAddress(advapi, "RegQueryValueExW"));
		auto close = reinterpret_cast<reg_close_key>(GetProcAddress(advapi, "RegCloseKey"));
		if (open == nullptr || query == nullptr || close == nullptr)
		{
			FreeLibrary(advapi);
			return;
		}

		HKEY key = nullptr;
		const wchar_t* subkey = L"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\";
		std::wstring key_name = std::wstring(subkey) + architecture;
		if (open(HKEY_LOCAL_MACHINE, key_name.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS)
		{
			wchar_t value[32768];
			DWORD type = 0;
			DWORD size = sizeof(value);
			if (query(key, L"InstallLocation", nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS &&
				(type == REG_SZ || type == REG_EXPAND_SZ) && size >= sizeof(wchar_t))
			{
				value[(size / sizeof(wchar_t)) - 1] = L'\0';
				roots.emplace_back(value);
			}
			close(key);
		}
		FreeLibrary(advapi);
	}
#endif

	static void add_default_roots(std::vector<fs::path>& roots)
	{
#ifdef _WIN32
#if defined(_M_X64) || defined(__x86_64__)
		string_t architecture = L"X64";
		const wchar_t* registry_architecture = L"x64";
#elif defined(_M_ARM64)
		string_t architecture = L"Arm64";
		const wchar_t* registry_architecture = L"arm64";
#else
		string_t architecture = L"X86";
		const wchar_t* registry_architecture = L"x86";
#endif
		roots.emplace_back(environment((L"DOTNET_ROOT_" + architecture).c_str()));
		roots.emplace_back(environment(L"DOTNET_ROOT"));
		add_registry_root(roots, registry_architecture);
		wchar_t program_files[32768];
		DWORD length = GetEnvironmentVariableW(L"ProgramW6432", program_files, 32768);
		if (length == 0)
			length = GetEnvironmentVariableW(L"ProgramFiles", program_files, 32768);
		if (length != 0)
			roots.emplace_back(std::wstring(program_files, length) + L"\\dotnet");
#else
#if defined(__x86_64__)
		roots.emplace_back(environment("DOTNET_ROOT_X64"));
#elif defined(__aarch64__)
		roots.emplace_back(environment("DOTNET_ROOT_ARM64"));
#elif defined(__i386__)
		roots.emplace_back(environment("DOTNET_ROOT_X86"));
#endif
		roots.emplace_back(environment("DOTNET_ROOT"));
#if defined(__APPLE__)
		roots.emplace_back("/usr/local/share/dotnet");
#else
		roots.emplace_back("/usr/share/dotnet");
		roots.emplace_back("/usr/local/share/dotnet");
#endif
#endif
	}

	static fs::path existing_hostfxr()
	{
#ifdef _WIN32
		HMODULE module = GetModuleHandleW(hostfxr_name);
		if (module == nullptr)
			return {};
		wchar_t path[32768];
		DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
		return length == 0 ? fs::path{} : fs::path(path, path + length);
#elif defined(__APPLE__)
		for (uint32_t i = 0; i < _dyld_image_count(); ++i)
		{
			const char* image = _dyld_get_image_name(i);
			if (image != nullptr && fs::path(image).filename().string() == hostfxr_name)
				return fs::path(image);
		}
		return {};
#else
		void* module = dlopen(hostfxr_name, RTLD_LAZY | RTLD_NOLOAD);
		if (module == nullptr)
			return {};
		link_map* map = nullptr;
		fs::path result;
		if (dlinfo(module, RTLD_DI_LINKMAP, &map) == 0 && map != nullptr && map->l_name != nullptr)
			result = fs::path(map->l_name);
		dlclose(module);
		return result;
#endif
	}

	static fs::path find_hostfxr(const get_hostfxr_parameters* parameters)
	{
		fs::path loaded = existing_hostfxr();
		if (!loaded.empty())
			return loaded;

		if (parameters != nullptr && parameters->dotnet_root != nullptr)
			return hostfxr_in(fs::path(parameters->dotnet_root));

		std::vector<fs::path> roots;
		if (parameters != nullptr && parameters->assembly_path != nullptr)
		{
			fs::path app_local = hostfxr_in(fs::path(parameters->assembly_path).parent_path());
			if (!app_local.empty())
				return app_local;
		}
		add_default_roots(roots);
		for (const fs::path& root : roots)
		{
			if (!root.empty())
			{
				fs::path result = hostfxr_in(root);
				if (!result.empty())
					return result;
			}
		}
		return {};
	}
}

NETHOST_API int NETHOST_CALLTYPE get_hostfxr_path(
	char_t* buffer,
	size_t* buffer_size,
	const struct get_hostfxr_parameters* parameters)
{
	if (buffer_size == nullptr)
		return invalid_arg;

	constexpr size_t minimum_parameters_size = offsetof(get_hostfxr_parameters, dotnet_root) + sizeof(const char_t*);
	if (parameters != nullptr && parameters->size < minimum_parameters_size)
		return invalid_arg;

	fs::path path = find_hostfxr(parameters);
	if (path.empty())
		return hostfxr_missing;

	string_t value = path.native();
	if (value.size() == (std::numeric_limits<size_t>::max)())
		return hostfxr_missing;

	size_t required = value.size() + 1;
	size_t supplied = *buffer_size;
	*buffer_size = required;
	if (buffer == nullptr || supplied < required)
		return buffer_too_small;

	std::copy(value.begin(), value.end(), buffer);
	buffer[value.size()] = static_cast<char_t>(0);
	return success;
}
