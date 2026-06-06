
// Copyright Tim Cottrill 2025
// Release notes:
// First revision 3/25/25

/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non - commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain.We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors.We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to < https://unlicense.org/>
*/

#include "path_helper.h"
#include <windows.h> // For MAX_PATH & GetModuleFileName
#include "utf8conv.h"
#include "sys_log.h"

// This is a helper function to return the fullpath of a file in Unicode

//Unicode Version
std::wstring getpathU(const char* dir, const char* file)
{
	std::wstring path;
	wchar_t temppath[MAX_PATH] = { 0 }; // Buffer to hold the path

	DWORD length = GetModuleFileNameW(NULL, temppath, MAX_PATH);

	if (length == 0)
	{
		// If the function fails, it returns 0
		LOG_INFO("Failed to get the file path. Error: %ld\n", GetLastError());
	}

	// Find the last backslash and terminate the string there
	wchar_t* lastBackslash = wcsrchr(temppath, '\\');
	if (lastBackslash != NULL) {
		*lastBackslash = '\0'; // End the string at the last backslash
	}

	path.assign(temppath);

	if (dir)
	{
		path.append(win32::Utf8ToUtf16("\\"));
		path.append(win32::Utf8ToUtf16(dir));
	}
	if (file)
	{
		path.append(win32::Utf8ToUtf16("\\"));
		path.append(win32::Utf8ToUtf16(file));
	}

	LOG_INFO("getpathU returning path: %s", path.c_str());
	return path;
}


// This is the non-wide version of this code. 
std::string getpathM(const char* dir, const char* file)
{
	std::string path;
	char temppath[MAX_PATH] = { 0 }; // Buffer to hold the path

	DWORD length = GetModuleFileNameA(NULL, temppath, MAX_PATH);

	if (length == 0)
	{
		// If the function fails, it returns 0
		LOG_INFO("Failed to get the file path. Error: %ld\n", GetLastError());
	}

	// Find the last backslash and terminate the string there
	char* lastBackslash = strrchr(temppath, '\\');
	if (lastBackslash != NULL) {
		*lastBackslash = '\0'; // End the string at the last backslash
	}

	path.assign(temppath);

	if (dir)
	{
		path.append("\\");
		path.append(dir);
	}
	if (file)
	{
		path.append("\\");
		path.append(file);
	}

	LOG_INFO("getpathM returning path: %s", path.c_str());
	return path;
}


