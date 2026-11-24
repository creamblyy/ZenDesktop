/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
Standalone lightweight Win32 updater binary (no Qt dependencies).
*/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <wininet.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

namespace {

constexpr int kZenVersionCode = 2601;
constexpr wchar_t kUpdateManifestUrl[] = L"https://creamblyy.github.io/ZenDesktop/update.json";
constexpr wchar_t kDialogTitle[] = L"ZenDesktop Updater";

std::wstring Utf8ToWide(const std::string &utf8) {
	if (utf8.empty()) {
		return {};
	}
	const int needed = MultiByteToWideChar(
		CP_UTF8,
		0,
		utf8.data(),
		static_cast<int>(utf8.size()),
		nullptr,
		0);
	if (needed <= 0) {
		return {};
	}
	std::wstring result(static_cast<size_t>(needed), L'\0');
	MultiByteToWideChar(
		CP_UTF8,
		0,
		utf8.data(),
		static_cast<int>(utf8.size()),
		result.data(),
		needed);
	return result;
}

size_t FindJsonKeyValueStart(const std::string &json, const char *key) {
	const std::string pattern = std::string("\"") + key + "\"";
	size_t pos = 0;
	while ((pos = json.find(pattern, pos)) != std::string::npos) {
		size_t cur = pos + pattern.size();
		while (cur < json.size() && std::isspace(static_cast<unsigned char>(json[cur]))) {
			++cur;
		}
		if (cur < json.size() && json[cur] == ':') {
			++cur;
			while (cur < json.size() && std::isspace(static_cast<unsigned char>(json[cur]))) {
				++cur;
			}
			return cur;
		}
		pos += pattern.size();
	}
	return std::string::npos;
}

std::string ExtractJsonString(const std::string &json, const char *key) {
	size_t cur = FindJsonKeyValueStart(json, key);
	if (cur == std::string::npos || cur >= json.size() || json[cur] != '"') {
		return {};
	}
	++cur;
	std::string out;
	while (cur < json.size()) {
		const char ch = json[cur++];
		if (ch == '\\' && cur < json.size()) {
			const char esc = json[cur++];
			if (esc == '"' || esc == '\\' || esc == '/') {
				out.push_back(esc);
			} else if (esc == 'n') {
				out.push_back('\n');
			} else if (esc == 'r') {
				out.push_back('\r');
			} else if (esc == 't') {
				out.push_back('\t');
			} else {
				out.push_back(esc);
			}
		} else if (ch == '"') {
			break;
		} else {
			out.push_back(ch);
		}
	}
	return out;
}

int ExtractJsonInt(const std::string &json, const char *key) {
	size_t cur = FindJsonKeyValueStart(json, key);
	if (cur == std::string::npos || cur >= json.size()) {
		return 0;
	}
	bool neg = false;
	if (json[cur] == '-') {
		neg = true;
		++cur;
	}
	int val = 0;
	bool any = false;
	while (cur < json.size() && std::isdigit(static_cast<unsigned char>(json[cur]))) {
		any = true;
		val = val * 10 + (json[cur] - '0');
		++cur;
	}
	return any ? (neg ? -val : val) : 0;
}

bool HttpDownload(const std::wstring &url, std::string &outData) {
	outData.clear();
	HINTERNET session = InternetOpenW(
		L"ZenDesktop-Updater/1.0",
		INTERNET_OPEN_TYPE_PRECONFIG,
		nullptr,
		nullptr,
		0);
	if (!session) {
		return false;
	}

	DWORD flags = INTERNET_FLAG_RELOAD
		| INTERNET_FLAG_NO_CACHE_WRITE
		| INTERNET_FLAG_NO_UI;
	if (url.rfind(L"https://", 0) == 0) {
		flags |= INTERNET_FLAG_SECURE;
	}

	HINTERNET req = InternetOpenUrlW(session, url.c_str(), nullptr, 0, flags, 0);
	if (!req) {
		InternetCloseHandle(session);
		return false;
	}

	DWORD timeoutConnect = 15000;
	DWORD timeoutRecv = 60000;
	DWORD timeoutSend = 30000;
	InternetSetOptionW(req, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutConnect, sizeof(timeoutConnect));
	InternetSetOptionW(req, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutRecv, sizeof(timeoutRecv));
	InternetSetOptionW(req, INTERNET_OPTION_SEND_TIMEOUT, &timeoutSend, sizeof(timeoutSend));

	DWORD statusCode = 0;
	DWORD statusSize = sizeof(statusCode);
	if (HttpQueryInfoW(
			req,
			HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
			&statusCode,
			&statusSize,
			nullptr)) {
		if (statusCode < 200 || statusCode >= 300) {
			InternetCloseHandle(req);
			InternetCloseHandle(session);
			return false;
		}
	}

	DWORD expectedLen = 0;
	DWORD lenSize = sizeof(expectedLen);
	if (!HttpQueryInfoW(
			req,
			HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
			&expectedLen,
			&lenSize,
			nullptr)) {
		expectedLen = 0;
	}

	std::vector<char> buf(65536);
	DWORD bytesRead = 0;
	BOOL readOk = FALSE;
	while ((readOk = InternetReadFile(req, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead))
			&& bytesRead > 0) {
		outData.append(buf.data(), bytesRead);
	}

	InternetCloseHandle(req);
	InternetCloseHandle(session);
	return readOk && !outData.empty() && (expectedLen == 0 || outData.size() >= expectedLen);
}

bool WriteAllBytes(const std::wstring &path, const std::string &data) {
	HANDLE file = CreateFileW(
		path.c_str(),
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}

	size_t writtenTotal = 0;
	while (writtenTotal < data.size()) {
		const DWORD chunk = static_cast<DWORD>(
			std::min<size_t>(data.size() - writtenTotal, 1U << 20));
		DWORD writtenNow = 0;
		if (!WriteFile(file, data.data() + writtenTotal, chunk, &writtenNow, nullptr)
				|| writtenNow == 0) {
			CloseHandle(file);
			DeleteFileW(path.c_str());
			return false;
		}
		writtenTotal += writtenNow;
	}
	CloseHandle(file);
	return true;
}

bool CopyWithRetries(const std::wstring &src, const std::wstring &dst, int attempts = 30) {
	for (int i = 0; i < attempts; ++i) {
		if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
			return true;
		}
		Sleep(250);
	}
	return false;
}

bool LaunchBinary(const std::wstring &exePath, const std::wstring &extraArgs = {}) {
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::wstring cmdLine = L"\"" + exePath + L"\"";
	if (!extraArgs.empty()) {
		cmdLine += L" " + extraArgs;
	}
	if (!CreateProcessW(
			exePath.c_str(),
			cmdLine.data(),
			nullptr,
			nullptr,
			FALSE,
			0,
			nullptr,
			nullptr,
			&si,
			&pi)) {
		return false;
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return true;
}

std::wstring GetSiblingTargetPath() {
	wchar_t selfPath[MAX_PATH * 4]{};
	const DWORD len = GetModuleFileNameW(
		nullptr,
		selfPath,
		static_cast<DWORD>(sizeof(selfPath) / sizeof(wchar_t)));
	if (len == 0) {
		return L"ZenDesktop.exe";
	}
	std::wstring dir(selfPath, len);
	const size_t slash = dir.find_last_of(L"\\/");
	if (slash == std::wstring::npos) {
		return L"ZenDesktop.exe";
	}
	return dir.substr(0, slash + 1) + L"ZenDesktop.exe";
}

std::wstring BuildTempUpdatePath() {
	wchar_t tempDir[MAX_PATH * 2]{};
	const DWORD len = GetTempPathW(
		static_cast<DWORD>(sizeof(tempDir) / sizeof(wchar_t)),
		tempDir);
	if (len == 0) {
		return L"ZenDesktop_update.exe";
	}
	return std::wstring(tempDir, len) + L"ZenDesktop_update.exe";
}

int RunApplyMode(
		DWORD pid,
		const std::wstring &source,
		const std::wstring &target,
		const std::wstring &extraArgs = {}) {
	if (pid != 0) {
		HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
		if (proc) {
			WaitForSingleObject(proc, 30000);
			CloseHandle(proc);
		}
		Sleep(200);
	}

	if (!CopyWithRetries(source, target)) {
		MessageBoxW(
			nullptr,
			L"Не удалось заменить файл ZenDesktop.exe. Закройте приложение вручную и повторите попытку.",
			kDialogTitle,
			MB_OK | MB_ICONERROR);
		return 1;
	}

	DeleteFileW(source.c_str());
	LaunchBinary(target, extraArgs);
	return 0;
}

int RunInteractiveCheck() {
	std::string manifest;
	if (!HttpDownload(kUpdateManifestUrl, manifest)) {
		MessageBoxW(
			nullptr,
			L"Не удалось проверить обновления. Проверьте подключение к интернету.",
			kDialogTitle,
			MB_OK | MB_ICONERROR);
		return 1;
	}

	const std::string remoteVersion = ExtractJsonString(manifest, "version");
	const int remoteCode = ExtractJsonInt(manifest, "version_code");
	const std::string downloadUrl = ExtractJsonString(manifest, "url");

	if (remoteCode <= kZenVersionCode) {
		MessageBoxW(
			nullptr,
			L"У вас установлена последняя версия ZenDesktop.",
			kDialogTitle,
			MB_OK | MB_ICONINFORMATION);
		return 0;
	}

	if (downloadUrl.empty()) {
		MessageBoxW(
			nullptr,
			L"В манифесте обновлений не указана ссылка для скачивания.",
			kDialogTitle,
			MB_OK | MB_ICONERROR);
		return 1;
	}

	const std::wstring verWide = Utf8ToWide(remoteVersion);
	const std::wstring prompt = L"Доступна новая версия ZenDesktop "
		+ (verWide.empty() ? L"" : verWide)
		+ L".\n\nСкачать и установить обновление сейчас?";
	if (MessageBoxW(nullptr, prompt.c_str(), kDialogTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) {
		return 0;
	}

	std::string payload;
	if (!HttpDownload(Utf8ToWide(downloadUrl), payload)) {
		MessageBoxW(
			nullptr,
			L"Не удалось скачать файл обновления.",
			kDialogTitle,
			MB_OK | MB_ICONERROR);
		return 1;
	}

	if (payload.size() < 512 * 1024 || payload[0] != 'M' || payload[1] != 'Z') {
		MessageBoxW(
			nullptr,
			L"Скачанный файл обновления повреждён или не является исполняемым файлом Windows.",
			kDialogTitle,
			MB_OK | MB_ICONERROR);
		return 1;
	}

	const std::wstring tempPath = BuildTempUpdatePath();
	if (!WriteAllBytes(tempPath, payload)) {
		MessageBoxW(
			nullptr,
			L"Не удалось сохранить временный файл обновления.",
			kDialogTitle,
			MB_OK | MB_ICONERROR);
		return 1;
	}

	const std::wstring targetPath = GetSiblingTargetPath();
	while (!CopyWithRetries(tempPath, targetPath, 8)) {
		const int choice = MessageBoxW(
			nullptr,
			L"Не удалось обновить ZenDesktop.exe. Возможно, клиент сейчас запущен.\n\nЗакройте ZenDesktop и нажмите «Повтор».",
			kDialogTitle,
			MB_RETRYCANCEL | MB_ICONWARNING);
		if (choice != IDRETRY) {
			DeleteFileW(tempPath.c_str());
			return 1;
		}
	}

	DeleteFileW(tempPath.c_str());
	LaunchBinary(targetPath);
	return 0;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
	int argc = 0;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	DWORD pid = 0;
	std::wstring source;
	std::wstring target;
	std::wstring extraArgs;

	auto quoteArg = [](const std::wstring &arg) {
		if (arg.find_first_of(L" \t\"") == std::wstring::npos && !arg.empty()) {
			return arg;
		}
		std::wstring out = L"\"";
		for (wchar_t ch : arg) {
			if (ch == L'"') {
				out += L"\\\"";
			} else {
				out += ch;
			}
		}
		out += L"\"";
		return out;
	};

	if (argv) {
		for (int i = 1; i < argc; ++i) {
			if (wcscmp(argv[i], L"--") == 0) {
				for (int j = i + 1; j < argc; ++j) {
					if (!extraArgs.empty()) {
						extraArgs += L" ";
					}
					extraArgs += quoteArg(argv[j]);
				}
				break;
			}
			if (i + 1 >= argc) {
				break;
			}
			if (wcscmp(argv[i], L"--pid") == 0) {
				pid = static_cast<DWORD>(_wtoi(argv[++i]));
			} else if (wcscmp(argv[i], L"--source") == 0) {
				source = argv[++i];
			} else if (wcscmp(argv[i], L"--target") == 0) {
				target = argv[++i];
			}
		}
		LocalFree(argv);
	}

	if (!source.empty() && !target.empty()) {
		return RunApplyMode(pid, source, target, extraArgs);
	}
	return RunInteractiveCheck();
}
