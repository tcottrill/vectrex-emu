/* =============================================================================
 * File: sys_fileio.h
 * Component: File I/O utilities (public API)
 *
 * Overview
 * --------
 * Public interface for file input/output operations. Provides:
 *   - Functions to get the current directory
 *   - Functions to check for file and directory existence
 *   - Functions to get file sizes
 *   - Functions to load and save files
 *   - Functions to load files from zip archives		
 *	
 * ---------------------------------------------------------------------------
 * License (GPLv3):
 *   This file is part of GameEngine Alpha.
 *
 *   <Project Name> is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   <Project Name> is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with GameEngine Alpha.  If not, see <https://www.gnu.org/licenses/>.
 *
 *   Copyright (C) 2012-2025  Tim Cottrill
 *   SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */

#pragma once

#ifndef FILEIO_H
#define FILEIO_H

#include <string>
#include <cstdint>
#include <cstdio> // For FILE*

// Get Current Directory
std::wstring getCurrentDirectoryW();
std::string getCurrentDirectory();

// Path Management
bool DirectoryExists(const char* dirName);
bool fileExistsReadable(const char* filename);

// Get information on last file operation
size_t getLastFileSize();
int getFileSize(FILE* input);

// Get information on last compression/zip operation
size_t getLastZSize();
uint32_t getLastZCrc(); // For AAE compatibility

// Load/Save File
uint8_t* loadFile(const std::string& filename);
uint8_t* loadFile(const char* filename);
bool saveFile(const char* filename, const unsigned char* buf, int size);

// Load/Save Zip
unsigned char* loadZip(const char* archname, const char* filename);
bool saveZip(const char* archname, const char* filename, const unsigned char* data);

// File Manipulation
void replaceExtension(std::string& str, const std::string& rep);
std::string getBaseName(const std::string& path);
std::string removeExtension(const std::string& filename);

#endif