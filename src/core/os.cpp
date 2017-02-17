/*
 * Copyright (c) 2012-2017 Daniele Bartolini and individual contributors.
 * License: https://github.com/taylor001/crown/blob/master/LICENSE
 */

#include "dynamic_string.h"
#include "error.h"
#include "os.h"
#include "platform.h"
#include "string_stream.h"
#include "temp_allocator.h"
#include "vector.h"
#include <string.h> // strcmp

#if CROWN_PLATFORM_POSIX
	#include <dirent.h> // opendir, readdir
	#include <dlfcn.h>    // dlopen, dlclose, dlsym
	#include <errno.h>
	#include <stdio.h>    // fputs
	#include <stdlib.h>   // getenv
	#include <string.h>   // memset
	#include <sys/stat.h> // lstat, mknod, mkdir
	#include <sys/wait.h> // wait
	#include <time.h>     // clock_gettime
	#include <unistd.h>   // access, unlink, rmdir, getcwd, fork, execv
#elif CROWN_PLATFORM_WINDOWS
	#include <io.h>
	#include <stdio.h>
	#include <windows.h>
#endif
#if CROWN_PLATFORM_ANDROID
	#include <android/log.h>
#endif

namespace crown
{
namespace os
{
	s64 clocktime()
	{
#if CROWN_PLATFORM_LINUX || CROWN_PLATFORM_ANDROID
		timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		return now.tv_sec * s64(1000000000) + now.tv_nsec;
#elif CROWN_PLATFORM_OSX
		struct timeval now;
		gettimeofday(&now, NULL);
		return now.tv_sec * s64(1000000) + now.tv_usec;
#elif CROWN_PLATFORM_WINDOWS
		LARGE_INTEGER ttime;
		QueryPerformanceCounter(&ttime);
		return (s64)ttime.QuadPart;
#endif
	}

	s64 clockfrequency()
	{
#if CROWN_PLATFORM_LINUX || CROWN_PLATFORM_ANDROID
		return s64(1000000000);
#elif CROWN_PLATFORM_OSX
		return s64(1000000);
#elif CROWN_PLATFORM_WINDOWS
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		return (s64)freq.QuadPart;
#endif
	}

	/// Suspends execution for @a ms milliseconds.
	void sleep(u32 ms)
	{
#if CROWN_PLATFORM_POSIX
		usleep(ms * 1000);
#elif CROWN_PLATFORM_WINDOWS
		Sleep(ms);
#endif
	}

	/// Opens the library at @a path.
	void* library_open(const char* path)
	{
#if CROWN_PLATFORM_POSIX
		return ::dlopen(path, RTLD_LAZY);
#elif CROWN_PLATFORM_WINDOWS
		return (void*)LoadLibraryA(path);
#endif
	}

	/// Closes a @a library previously opened by library_open.
	void library_close(void* library)
	{
#if CROWN_PLATFORM_POSIX
		dlclose(library);
#elif CROWN_PLATFORM_WINDOWS
		FreeLibrary((HMODULE)library);
#endif
	}

	/// Returns a pointer to the symbol @a name in the given @a library.
	void* library_symbol(void* library, const char* name)
	{
#if CROWN_PLATFORM_POSIX
		return ::dlsym(library, name);
#elif CROWN_PLATFORM_WINDOWS
		return (void*)GetProcAddress((HMODULE)library, name);
#endif
	}

	/// Logs the message @a msg.
	void log(const char* msg)
	{
#if CROWN_PLATFORM_ANDROID
		__android_log_write(ANDROID_LOG_DEBUG, "crown", msg);
#elif CROWN_PLATFORM_WINDOWS
		OutputDebugStringA(msg);
#else
		fputs(msg, stdout);
		fflush(stdout);
#endif
	}

	/// Returns whether the @a path exists.
	bool exists(const char* path)
	{
#if CROWN_PLATFORM_POSIX
		return access(path, F_OK) != -1;
#elif CROWN_PLATFORM_WINDOWS
		return _access(path, 0) != -1;
#endif
	}

	/// Returns whether @a path is a directory.
	bool is_directory(const char* path)
	{
#if CROWN_PLATFORM_POSIX
		struct stat info;
		memset(&info, 0, sizeof(info));
		int err = lstat(path, &info);
		CE_ASSERT(err == 0, "lstat: errno = %d", errno);
		CE_UNUSED(err);
		return ((S_ISDIR(info.st_mode) == 1) && (S_ISLNK(info.st_mode) == 0));
#elif CROWN_PLATFORM_WINDOWS
		DWORD fattr = GetFileAttributes(path);
		return (fattr != INVALID_FILE_ATTRIBUTES && (fattr & FILE_ATTRIBUTE_DIRECTORY) != 0);
#endif
	}

	/// Returns whether @a path is a regular file.
	bool is_file(const char* path)
	{
#if CROWN_PLATFORM_POSIX
		struct stat info;
		memset(&info, 0, sizeof(info));
		int err = lstat(path, &info);
		CE_ASSERT(err == 0, "lstat: errno = %d", errno);
		CE_UNUSED(err);
		return ((S_ISREG(info.st_mode) == 1) && (S_ISLNK(info.st_mode) == 0));
#elif CROWN_PLATFORM_WINDOWS
		DWORD fattr = GetFileAttributes(path);
		return (fattr != INVALID_FILE_ATTRIBUTES && (fattr & FILE_ATTRIBUTE_DIRECTORY) == 0);
#endif
	}

	/// Returns the last modification time of @a path.
	u64 mtime(const char* path)
	{
#if CROWN_PLATFORM_POSIX
		struct stat info;
		memset(&info, 0, sizeof(info));
		int err = lstat(path, &info);
		CE_ASSERT(err == 0, "lstat: errno = %d", errno);
		CE_UNUSED(err);
		return info.st_mtime;
#elif CROWN_PLATFORM_WINDOWS
		HANDLE hfile = CreateFile(path
			, GENERIC_READ
			, FILE_SHARE_READ
			, NULL
			, OPEN_EXISTING
			, 0
			, NULL
			);
		CE_ASSERT(hfile != INVALID_HANDLE_VALUE, "CreateFile: GetLastError = %d", GetLastError());
		FILETIME ftwrite;
		BOOL err = GetFileTime(hfile, NULL, NULL, &ftwrite);
		CE_ASSERT(err != 0, "GetFileTime: GetLastError = %d", GetLastError());
		CE_UNUSED(err);
		CloseHandle(hfile);
		return (u64)((u64(ftwrite.dwHighDateTime) << 32) | ftwrite.dwLowDateTime);
#endif
	}

	/// Creates a regular file named @a path.
	void create_file(const char* path)
	{
#if CROWN_PLATFORM_POSIX
		int err = ::mknod(path, 0644 | S_IFREG , 0);
		CE_ASSERT(err == 0, "mknod: errno = %d", errno);
		CE_UNUSED(err);
#elif CROWN_PLATFORM_WINDOWS
		HANDLE hfile = CreateFile(path
			, GENERIC_READ | GENERIC_WRITE
			, 0
			, NULL
			, CREATE_ALWAYS
			, FILE_ATTRIBUTE_NORMAL
			, NULL
			);
		CE_ASSERT(hfile != INVALID_HANDLE_VALUE, "CreateFile: GetLastError = %d", GetLastError());
		CloseHandle(hfile);
#endif
	}

	/// Deletes the file at @a path.
	void delete_file(const char* path)
	{
#if CROWN_PLATFORM_POSIX
		int err = ::unlink(path);
		CE_ASSERT(err == 0, "unlink: errno = %d", errno);
		CE_UNUSED(err);
#elif CROWN_PLATFORM_WINDOWS
		BOOL err = DeleteFile(path);
		CE_ASSERT(err != 0, "DeleteFile: GetLastError = %d", GetLastError());
		CE_UNUSED(err);
#endif
	}

	/// Creates a directory named @a path.
	void create_directory(const char* path)
	{
#if CROWN_PLATFORM_POSIX
		int err = ::mkdir(path, 0755);
		CE_ASSERT(err == 0, "mkdir: errno = %d", errno);
		CE_UNUSED(err);
#elif CROWN_PLATFORM_WINDOWS
		BOOL err = CreateDirectory(path, NULL);
		CE_ASSERT(err != 0, "CreateDirectory: GetLastError = %d", GetLastError());
		CE_UNUSED(err);
#endif
	}

	/// Deletes the directory at @a path.
	void delete_directory(const char* path)
	{
#if CROWN_PLATFORM_POSIX
		int err = ::rmdir(path);
		CE_ASSERT(err == 0, "rmdir: errno = %d", errno);
		CE_UNUSED(err);
#elif CROWN_PLATFORM_WINDOWS
		BOOL err = RemoveDirectory(path);
		CE_ASSERT(err != 0, "RemoveDirectory: GetLastError = %d", GetLastError());
		CE_UNUSED(err);
#endif
	}

	/// Returns the list of @a files at the given @a path.
	void list_files(const char* path, Vector<DynamicString>& files);

	/// Returns the current working directory.
	const char* getcwd(char* buf, u32 size)
	{
#if CROWN_PLATFORM_POSIX
		return ::getcwd(buf, size);
#elif CROWN_PLATFORM_WINDOWS
		GetCurrentDirectory(size, buf);
		return buf;
#endif
	}

	/// Returns the value of the environment variable @a name.
	const char* getenv(const char* name)
	{
#if CROWN_PLATFORM_POSIX
		return ::getenv(name);
#elif CROWN_PLATFORM_WINDOWS
		// GetEnvironmentVariable(name, buf, size);
		return NULL;
#endif
	}

	void list_files(const char* path, Vector<DynamicString>& files)
	{
#if CROWN_PLATFORM_POSIX
		DIR *dir;
		struct dirent *entry;

		if (!(dir = opendir(path)))
			return;

		while ((entry = readdir(dir)))
		{
			const char* dname = entry->d_name;

			if (!strcmp(dname, ".") || !strcmp(dname, ".."))
				continue;

			TempAllocator512 ta;
			DynamicString fname(ta);
			fname.set(dname, strlen32(dname));
			vector::push_back(files, fname);
		}

		closedir(dir);
#elif CROWN_PLATFORM_WINDOWS
		TempAllocator1024 ta;
		DynamicString cur_path(ta);
		cur_path += path;
		cur_path += "\\*";

		WIN32_FIND_DATA ffd;
		HANDLE file = FindFirstFile(cur_path.c_str(), &ffd);
		if (file == INVALID_HANDLE_VALUE)
			return;

		do
		{
			const char* dname = ffd.cFileName;

			if (!strcmp(dname, ".") || !strcmp(dname, ".."))
				continue;

			TempAllocator512 ta;
			DynamicString fname(ta);
			fname.set(dname, strlen32(dname));
			vector::push_back(files, fname);
		}
		while (FindNextFile(file, &ffd) != 0);

		FindClose(file);
#endif
	}

	int execute_process(const char* path, const char* args, StringStream& output)
	{
#if CROWN_PLATFORM_POSIX
		TempAllocator512 ta;
		DynamicString cmd(ta);
		cmd += path;
		cmd += " 2>&1 ";
		cmd += args;
		FILE* file = popen(cmd.c_str(), "r");

		char buf[1024];
		while (fgets(buf, sizeof(buf), file) != NULL)
			output << buf;

		return pclose(file);
#elif CROWN_PLATFORM_WINDOWS
		STARTUPINFO info;
		memset(&info, 0, sizeof(info));
		info.cb = sizeof(info);

		PROCESS_INFORMATION process;
		memset(&process, 0, sizeof(process));

		int err = CreateProcess(path, (LPSTR)args, NULL, NULL, TRUE, 0, NULL, NULL, &info, &process);
		CE_ASSERT(err != 0, "CreateProcess: GetLastError = %d", GetLastError());
		CE_UNUSED(err);

		DWORD exitcode = 1;
		::WaitForSingleObject(process.hProcess, INFINITE);
		GetExitCodeProcess(process.hProcess, &exitcode);
		CloseHandle(process.hProcess);
		CloseHandle(process.hThread);
		return (int)exitcode;
#endif
	}

} // namespace os
} // namespace crown
