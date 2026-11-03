#include "license.h"
#include <iomanip>
#include <fstream>
#include <sstream>
#include <iostream>
#include <mutex>
#include <shared_mutex>

#include "../network/sha.h"
#include "../network/aes.h"
#include "../network/uuid.h"
#include "../network/base64.h"
#include "../software/system/timesys.h"
#include "../software/system/codesys.h"

// 平台检测
#if defined(_WIN32)
#define PLATFORM_WINDOWS
#include <intrin.h>
#include <comdef.h>
#include <Windows.h>
#include <Wbemidl.h>
#include <winioctl.h>
#pragma comment(lib, "wbemuuid.lib")
#elif defined(__linux__)
#define PLATFORM_LINUX
#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>
#elif defined(__APPLE__)
#define PLATFORM_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/IOKitLib.h>
#include <sys/utsname.h>
#endif

namespace xhqm
{
	namespace license
	{
		std::shared_mutex _mutex_;
		const xhqm::text _version_ = "version";
		const xhqm::text _program_ = "program";
		const xhqm::text _listall_ = "alllist";
		const xhqm::text _timeall_ = "endtime";

		xhqm::text all_version(void) { std::shared_lock<std::shared_mutex> lock(_mutex_); return _version_; };
		xhqm::text all_program(void) { std::shared_lock<std::shared_mutex> lock(_mutex_); return _program_; };
		xhqm::text all_listhas(void) { std::shared_lock<std::shared_mutex> lock(_mutex_); return _listall_; };
		xhqm::text all_timehas(void) { std::shared_lock<std::shared_mutex> lock(_mutex_); return _timeall_; };

		std::string _trim(const std::string& str)
		{
			const char* whitespace = " \t\n\r\f\v";
			std::string result = str;

			// 删除右侧空白
			result.erase(result.find_last_not_of(whitespace) + 1);
			// 删除左侧空白
			result.erase(0, result.find_first_not_of(whitespace));

			return result;
		}

		std::string _exec(const std::string& cmd) {
#if defined(PLATFORM_WINDOWS)
			std::string result = "";
			FILE* pipe = _popen(cmd.c_str(), "r");
			if (!pipe) return "";

			char buffer[128];
			while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
				result += buffer;
			}
			_pclose(pipe);

			return _trim(result);
#else
			std::string result = "";
			FILE* pipe = popen(cmd.c_str(), "r");
			if (!pipe) return "";

			char buffer[128];
			while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
				result += buffer;
			}
			pclose(pipe);

			return _trim(result);
#endif
		}

		bool _is_know(std::string uuid)
		{
			std::vector<std::string> placeholders =
			{
				"00000000-0000-0000-0000-000000000000", "ffffffff-ffff-ffff-ffff-ffffffffffff",
				"12345678-1234-1234-1234-123456789abc", "1234567890", "0000000000", "FFFFFFFF",
				"To be filled", "Default string", "Not Available", "Not Specified", "System Serial#", ""
			};
			std::transform(uuid.begin(), uuid.end(), uuid.begin(), [](auto c) { return std::tolower(c); });
			return std::find(placeholders.begin(), placeholders.end(), uuid) != placeholders.end();
		}


#ifdef PLATFORM_WINDOWS // Windows实现

		std::string GetWindowsMachineGuid() {
			HKEY hKey;
			std::string guid;

			// 打开注册表键
			if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
				"SOFTWARE\\Microsoft\\Cryptography",
				0, KEY_READ, &hKey) == ERROR_SUCCESS) {

				DWORD dataType = REG_SZ;
				DWORD dataSize = 0;

				// 获取MachineGuid值的大小
				if (RegQueryValueExA(hKey, "MachineGuid", nullptr,
					&dataType, nullptr, &dataSize) == ERROR_SUCCESS) {

					std::vector<char> buffer(dataSize);

					// 读取MachineGuid值
					if (RegQueryValueExA(hKey, "MachineGuid", nullptr,
						&dataType, (LPBYTE)buffer.data(),
						&dataSize) == ERROR_SUCCESS) {
						guid.assign(buffer.data());
					}
				}

				RegCloseKey(hKey);
			}

			return guid;  // 格式: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
		}
		std::string GetWindowsInstallationID() {
			HKEY hKey;
			std::string installID;

			// 尝试多个可能的注册表位置
			const char* paths[] = {
				"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
				"SOFTWARE\\Microsoft\\Windows\\CurrentVersion"
			};

			for (const char* path : paths) {
				if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
					DWORD dataType = REG_SZ;
					DWORD dataSize = 0;

					// 尝试读取InstallationID
					if (RegQueryValueExA(hKey, "InstallationID", nullptr,
						&dataType, nullptr, &dataSize) == ERROR_SUCCESS) {
						std::vector<char> buffer(dataSize);
						if (RegQueryValueExA(hKey, "InstallationID", nullptr,
							&dataType, (LPBYTE)buffer.data(),
							&dataSize) == ERROR_SUCCESS) {
							installID.assign(buffer.data());
							break;
						}
					}

					// 如果没有InstallationID，尝试ProductID
					if (installID.empty()) {
						dataSize = 0;
						if (RegQueryValueExA(hKey, "ProductId", nullptr,
							&dataType, nullptr, &dataSize) == ERROR_SUCCESS) {
							std::vector<char> buffer(dataSize);
							if (RegQueryValueExA(hKey, "ProductId", nullptr,
								&dataType, (LPBYTE)buffer.data(),
								&dataSize) == ERROR_SUCCESS) {
								installID.assign(buffer.data());
								break;
							}
						}
					}

					RegCloseKey(hKey);
				}
			}

			return installID;
		}

		std::string cpu_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			int CPUInfo[4] = { -1 };
			char CPUBrandString[0x40] = { 0 };

			// 获取厂商标识
			__cpuid(CPUInfo, 0);
			memcpy(CPUBrandString, &CPUInfo[1], 4);
			memcpy(CPUBrandString + 4, &CPUInfo[3], 4);
			memcpy(CPUBrandString + 8, &CPUInfo[2], 4);

			// 获取处理器序列号
			__cpuid(CPUInfo, 1);
			unsigned int s1 = CPUInfo[3];  // EDX
			unsigned int s2 = CPUInfo[0];  // EAX

			char serial[32];
			sprintf_s(serial, "%08X%08X", s1, s2);
			return _trim(std::string(serial));
		}

		std::string disk_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			std::wstring path = L"\\\\.\\PhysicalDrive0";
			HANDLE hDevice = CreateFileW(path.c_str(), 0,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL, OPEN_EXISTING, 0, NULL);

			if (hDevice == INVALID_HANDLE_VALUE) return "";

			STORAGE_PROPERTY_QUERY query{};
			query.PropertyId = StorageDeviceProperty;
			query.QueryType = PropertyStandardQuery;

			STORAGE_DESCRIPTOR_HEADER header{};
			DWORD bytesReturned = 0;

			DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
				&query, sizeof(query),
				&header, sizeof(header),
				&bytesReturned, NULL);

			DWORD bufSize = header.Size;
			BYTE* buffer = new BYTE[bufSize];

			DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
				&query, sizeof(query),
				buffer, bufSize,
				&bytesReturned, NULL);

			STORAGE_DEVICE_DESCRIPTOR* desc =
				reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer);

			std::string serial;
			if (desc->SerialNumberOffset) {
				serial = reinterpret_cast<char*>(buffer + desc->SerialNumberOffset);
				serial = _trim(serial);
			}

			delete[] buffer;
			CloseHandle(hDevice);
			return serial;
		}

		std::string board_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			std::string result = "";

			// 方法1: 尝试WMI获取
			HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
			if (FAILED(hres)) {
				return "";
			}

			hres = CoInitializeSecurity(NULL, -1, NULL, NULL,
				RPC_C_AUTHN_LEVEL_DEFAULT,
				RPC_C_IMP_LEVEL_IMPERSONATE,
				NULL, EOAC_NONE, NULL);
			if (FAILED(hres)) {
				CoUninitialize();
				return "";
			}

			IWbemLocator* pLoc = NULL;
			hres = CoCreateInstance(CLSID_WbemLocator, 0,
				CLSCTX_INPROC_SERVER,
				IID_IWbemLocator, (LPVOID*)&pLoc);
			if (FAILED(hres)) {
				CoUninitialize();
				return "";
			}

			IWbemServices* pSvc = NULL;
			hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL,
				0, NULL, 0, 0, &pSvc);
			if (FAILED(hres)) {
				pLoc->Release();
				CoUninitialize();
				return "";
			}

			hres = CoSetProxyBlanket(
				pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
				NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
				NULL, EOAC_NONE
			);
			if (FAILED(hres)) {
				pSvc->Release();
				pLoc->Release();
				CoUninitialize();
				return "";
			}

			//获取主板序列号
			if (result.empty())
			{
				IEnumWbemClassObject* pEnumerator = NULL;
				hres = pSvc->ExecQuery(
					bstr_t("WQL"), bstr_t("SELECT SerialNumber FROM Win32_BaseBoard"),
					WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
					NULL, &pEnumerator
				);
				if (SUCCEEDED(hres)) {
					IWbemClassObject* pclsObj = NULL;
					ULONG uReturn = 0;

					while (pEnumerator) {
						HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
						if (0 == uReturn) { break; }
						VARIANT vtProp{};

						hr = pclsObj->Get(L"SerialNumber", 0, &vtProp, 0, 0);
						if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR) {
							char buffer[256];
							WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1,
								buffer, sizeof(buffer), NULL, NULL);
							result = buffer;
						}
						VariantClear(&vtProp);
						pclsObj->Release();

						if (!result.empty() && result != "Default string") {
							break;  // 找到有效序列号
						}
					}

					pEnumerator->Release();
					result = _trim(result);
				}
			}

			//获取系统序列号
			if (result.empty())
			{
				IEnumWbemClassObject* pEnumerator = NULL;
				hres = pSvc->ExecQuery(
					bstr_t("WQL"), bstr_t("SELECT IdentifyingNumber FROM Win32_ComputerSystemProduct"),
					WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
					NULL, &pEnumerator);
				if (SUCCEEDED(hres)) {
					IWbemClassObject* pclsObj = NULL;
					ULONG uReturn = 0;

					while (pEnumerator) {
						HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
						if (0 == uReturn) { break; }
						VARIANT vtProp{};

						hr = pclsObj->Get(L"IdentifyingNumber", 0, &vtProp, 0, 0);
						if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR) {
							char buffer[256];
							WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1,
								buffer, sizeof(buffer), NULL, NULL);
							result = buffer;
						}

						VariantClear(&vtProp);
						pclsObj->Release();

						if (!result.empty()) {
							break;  // 找到有效序列号
						}
					}

					pEnumerator->Release();
					result = _trim(result);
				}
			}

			pSvc->Release();
			pLoc->Release();
			CoUninitialize();
			return result;
		}

		std::string system_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			std::string result = GetWindowsMachineGuid();
			if (_is_know(result))
			{
				result = GetWindowsInstallationID();
			}
			return _trim(result);
		}

#endif // PLATFORM_WINDOWS


#ifdef PLATFORM_LINUX // Linux实现

		std::string cpu_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			std::string serial;

			std::string line;
			std::ifstream cpuinfo("/proc/cpuinfo");
			while (std::getline(cpuinfo, line)) {
				if (line.find("serial") != std::string::npos) {
					size_t pos = line.find(":");
					if (pos != std::string::npos) {
						serial = line.substr(pos + 2);
						break;
					}
				}
			}

			return serial;
		}

		std::string disk_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			std::ifstream serial_file("/sys/block/sda/device/serial");
			std::string serial;
			if (serial_file)
			{
				std::getline(serial_file, serial);
				return serial;
			}
			return serial;
		}

		std::string board_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			std::ifstream file("/sys/class/dmi/id/product_uuid");
			std::string serial;
			if (file.is_open())
			{
				std::getline(file, serial);
				file.close();
			}
			return serial;
		}

		std::string system_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			std::string machineID;

			// 尝试读取machine-id文件
			std::ifstream file("/etc/machine-id");
			if (file.is_open()) {
				std::getline(file, machineID);
				file.close();

				// 如果是有效的ID（32个十六进制字符）
				if (machineID.length() == 32) {
					return machineID;
				}
			}
			// 尝试dbus的machine-id
			file.open("/var/lib/dbus/machine-id");
			if (file.is_open()) {
				std::getline(file, machineID);
				file.close();
			}

			return machineID;
		}

#endif // PLATFORM_LINUX


#ifdef PLATFORM_MACOS // macOS实现

		std::string GetMacOSPlatformUUID() {
			std::string uuid;

			// 获取平台UUID
			io_service_t platformExpert = IOServiceGetMatchingService(
				kIOMasterPortDefault, IOServiceMatching("IOPlatformExpertDevice"));

			if (platformExpert) {
				CFTypeRef uuidRef = IORegistryEntryCreateCFProperty(
					platformExpert, CFSTR(kIOPlatformUUIDKey),
					kCFAllocatorDefault, 0);

				if (uuidRef) {
					char uuidStr[64];
					CFStringGetCString((CFStringRef)uuidRef, uuidStr,
						sizeof(uuidStr), kCFStringEncodingUTF8);
					uuid = uuidStr;
					CFRelease(uuidRef);
				}

				IOObjectRelease(platformExpert);
			}

			return uuid;
		}
		std::string GetMacOSSerialNumber() {
			std::string serial;

			// 通过系统调用获取序列号
			FILE* pipe = popen("system_profiler SPHardwareDataType | grep 'Serial Number' | awk '{print $4}'", "r");
			if (pipe) {
				char buffer[128];
				if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
					serial = buffer;
					// 去除换行符
					if (!serial.empty() && serial.back() == '\n') {
						serial.pop_back();
					}
				}
				pclose(pipe);
			}

			return serial;
		}

		std::string cpu_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			io_service_t platformExpert = IOServiceGetMatchingService(
				kIOMainPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
			if (!platformExpert) return "";
			CFStringRef serialNumber = (CFStringRef)IORegistryEntryCreateCFProperty(
				platformExpert, CFSTR(kIOPlatformSerialNumberKey), kCFAllocatorDefault, 0);
			IOObjectRelease(platformExpert);
			if (!serialNumber) return "";
			char buffer[256];
			CFStringGetCString(serialNumber, buffer, 256, kCFStringEncodingUTF8);
			CFRelease(serialNumber);
			return std::string(buffer);
		}

		std::string disk_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			std::string serialNumber = "";

			// 查找所有存储设备
			CFMutableDictionaryRef matching = IOServiceMatching("IOBlockStorageDevice");
			if (!matching) {
				return serialNumber;
			}

			// 获取服务迭代器
			io_iterator_t iterator = 0;
			kern_return_t kr = IOServiceGetMatchingServices(kIOMasterPortDefault,
				matching,
				&iterator);
			if (kr != KERN_SUCCESS) {
				return serialNumber;
			}

			io_object_t service = 0;
			bool foundBootDisk = false;

			while ((service = IOIteratorNext(iterator)) != 0 && !foundBootDisk) {
				CFMutableDictionaryRef properties = NULL;

				// 获取设备属性
				kr = IORegistryEntryCreateCFProperties(service,
					&properties,
					kCFAllocatorDefault,
					kNilOptions);

				if (kr == KERN_SUCCESS && properties) {
					// 获取设备特征
					CFDictionaryRef deviceCharacteristics = (CFDictionaryRef)CFDictionaryGetValue(
						properties, CFSTR(kIOPropertyDeviceCharacteristicsKey));

					if (deviceCharacteristics) {
						// 获取序列号
						CFStringRef serial = (CFStringRef)CFDictionaryGetValue(
							deviceCharacteristics, CFSTR(kIOPropertyProductSerialNumberKey));

						if (serial && CFGetTypeID(serial) == CFStringGetTypeID()) {
							char buffer[256];
							if (CFStringGetCString(serial, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
								serialNumber = buffer;
								foundBootDisk = true;
							}
						}
					}

					// 如果没有找到，尝试从其他键获取
					if (serialNumber.empty()) {
						// 尝试从协议特征获取
						CFDictionaryRef protocolCharacteristics = (CFDictionaryRef)CFDictionaryGetValue(
							properties, CFSTR(kIOPropertyProtocolCharacteristicsKey));

						if (protocolCharacteristics) {
							CFStringRef serial = (CFStringRef)CFDictionaryGetValue(
								protocolCharacteristics, CFSTR(kIOPropertyProductSerialNumberKey));

							if (serial && CFGetTypeID(serial) == CFStringGetTypeID()) {
								char buffer[256];
								if (CFStringGetCString(serial, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
									serialNumber = buffer;
									foundBootDisk = true;
								}
							}
						}
					}

					CFRelease(properties);
				}

				IOObjectRelease(service);
			}

			IOObjectRelease(iterator);

			return serialNumber;
		}

		std::string board_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			std::string result = "";

			// 方法1: 通过IOKit获取
			io_registry_entry_t platformExpert =
				IOServiceGetMatchingService(kIOMasterPortDefault,
					IOServiceMatching("IOPlatformExpertDevice"));

			if (platformExpert) {
				CFTypeRef serialNumberRef =
					IORegistryEntryCreateCFProperty(platformExpert,
						CFSTR(kIOPlatformSerialNumberKey),
						kCFAllocatorDefault, 0);

				if (serialNumberRef) {
					if (CFGetTypeID(serialNumberRef) == CFStringGetTypeID()) {
						CFStringRef serialString = (CFStringRef)serialNumberRef;
						char buffer[256];

						if (CFStringGetCString(serialString, buffer,
							sizeof(buffer), kCFStringEncodingUTF8)) {
							result = buffer;
						}
					}
					CFRelease(serialNumberRef);
				}

				IOObjectRelease(platformExpert);
			}
			return result;
		}

		std::string system_serial()
		{
			std::unique_lock<std::shared_mutex> lock(_mutex_);

			std::string result = GetMacOSPlatformUUID();
			if (_is_know(result))
			{
				result = GetMacOSSerialNumber();
			}
			return _trim(result);
		}

#endif // PLATFORM_MACOS

		std::string get_idcard(void)
		{
			auto uuid = xhqm::cipher::uuid::creat_uuid_v4({});
			return xhqm::cipher::uuid::to_text(uuid);
		}

		std::string get_device(void)
		{
			std::string serial = system_serial();
			if (_is_know(serial))
			{
				serial += ("cpu_serial:" + cpu_serial() + "\t");
				serial += ("disk_serial:" + disk_serial() + "\t");
				serial += ("board_serial:" + board_serial() + "\t");
				serial += ("system_serial:" + system_serial() + "\t");
			}
			return _trim(serial);
		}


		license::server base64_to_server(const xhqm::text& base64)
		{
			license::server server;
			xhqm::text text = xhqm::cipher::base64::decode_text(base64);
			std::vector<xhqm::text> server_str = xhqm::codesys::split_string(text, ' ');
			if (server_str.size() == 8)
			{
				if (server_str[7] == "server")
				{
					server.version = xhqm::cipher::base64::decode_text(server_str[0]);
					server.program = xhqm::cipher::base64::decode_text(server_str[1]);
					server.publish = xhqm::cipher::base64::decode_text(server_str[2]);
					server.company = xhqm::cipher::base64::decode_text(server_str[3]);
					server.listhas = xhqm::cipher::base64::decode_text(server_str[4]);
					server.timehas = xhqm::cipher::base64::decode_text(server_str[5]);
					server.license = xhqm::cipher::base64::decode_text(server_str[6]);
				}
			}
			return server;
		}

		xhqm::text server_to_base64(const license::server& server)
		{
			xhqm::text text;
			text += xhqm::cipher::base64::encode_text(server.version) + " ";
			text += xhqm::cipher::base64::encode_text(server.program) + " ";
			text += xhqm::cipher::base64::encode_text(server.publish) + " ";
			text += xhqm::cipher::base64::encode_text(server.company) + " ";
			text += xhqm::cipher::base64::encode_text(server.listhas) + " ";
			text += xhqm::cipher::base64::encode_text(server.timehas) + " ";
			text += xhqm::cipher::base64::encode_text(server.license) + " " + "server";
			return xhqm::cipher::base64::encode_text(text);
		}

		license::client base64_to_client(const xhqm::text& base64)
		{
			license::client client;
			xhqm::text text = xhqm::cipher::base64::decode_text(base64);
			std::vector<xhqm::text> client_str = xhqm::codesys::split_string(text, ' ');
			if (client_str.size() == 5)
			{
				if (client_str[4] == "client")
				{
					client.author = xhqm::cipher::base64::decode_text(client_str[0]);
					client.device = xhqm::cipher::base64::decode_text(client_str[1]);
					client.idcard = xhqm::cipher::base64::decode_text(client_str[2]);
					client.remark = xhqm::cipher::base64::decode_text(client_str[3]);
				}
			}
			return client;
		}

		xhqm::text client_to_base64(const license::client& client)
		{
			xhqm::text text;
			text += xhqm::cipher::base64::encode_text(client.author) + " ";
			text += xhqm::cipher::base64::encode_text(client.device) + " ";
			text += xhqm::cipher::base64::encode_text(client.idcard) + " ";
			text += xhqm::cipher::base64::encode_text(client.remark) + " " + "client";
			return xhqm::cipher::base64::encode_text(text);
		}

		bool read_license(const xhqm::text& base64, license::client& client, license::server& server)
		{
			xhqm::cipher::sha sha(xhqm::cipher::sha3_256);
			std::string key = sha(license::get_device());
			xhqm::cipher::aes aes(xhqm::cipher::aes::type::aes_256);
			aes.set_key(std::vector<xhqm::byte>(key.begin(), key.end()));
			auto text_vec = aes.decrypt(xhqm::cipher::base64::decode(base64));
			std::string text(text_vec.begin(), text_vec.end());

			std::vector<xhqm::text> license_str = xhqm::codesys::split_string(text, ' ');
			client = license::client();
			server = license::server();
			if (license_str.size() == 3)
			{
				if (license_str[2] == "license")
				{
					client = base64_to_client(license_str[0]);
					server = base64_to_server(license_str[1]);
				}
			}
			return (!client.idcard.empty());
		}

		bool save_license(const license::client& client, const license::server& server, xhqm::text& base64)
		{
			if (client.idcard.empty()) { return false; }
			if (client.device.empty()) { return false; }
			if (server.license.empty()) { return false; }
			if (server.version.empty()) { return false; }
			if (server.program.empty()) { return false; }
			if (server.listhas.empty()) { return false; }
			if (server.timehas.empty()) { return false; }

			xhqm::text client_64 = license::client_to_base64(client);
			xhqm::text server_64 = license::server_to_base64(server);
			xhqm::text text = client_64 + " " + server_64 + " " + "license";

			xhqm::cipher::sha sha(xhqm::cipher::sha3_256);
			std::string key = sha(client.device);
			xhqm::cipher::aes aes(xhqm::cipher::aes::type::aes_256);
			aes.set_key(std::vector<xhqm::byte>(key.begin(), key.end()));
			auto data = aes.encrypt(std::vector<xhqm::byte>(text.begin(), text.end()));
			base64 = xhqm::cipher::base64::encode(data);
			return true;
		}

		bool good::operator()(const std::string& license_path)
		{
			bool res = true;
			xhqm::text license;
			std::ifstream file(license_path);
			res &= file.is_open();
			if (res)
			{
				file >> license;
				file.close();
			}
			if (res && read_license(license, client_data, server_data))
			{
				if (client_data.device != get_device())
				{
					res = false;
				}
				if (good_version && res)
				{
					res = good_version(server_data.version);
				}
				if (good_program && res)
				{
					res = good_program(server_data.program);
				}
				if (server_data.listhas != all_listhas() && res)
				{
					res = false;
					if (good_listhas)
					{
						res = good_listhas(server_data.listhas);
					}
				}
				if (server_data.timehas != all_timehas() && res)
				{
					res = false;
					if (good_timehas)
					{
						res = good_timehas(server_data.timehas);
					}
				}
			}
			return res;
		}
		bool good::operator>>(const std::string& license_path)
		{
			xhqm::text license = "";
			if (license::save_license(client_data, server_data, license))
			{
				std::ofstream file(license_path);
				if (file.is_open())
				{
					file << license;
					file.close();
					return true;
				}
			}
			return false;
		}
	}
}
