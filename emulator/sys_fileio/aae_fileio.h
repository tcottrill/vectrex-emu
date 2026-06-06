// -----------------------------------------------------------------------------
// This file is part of the AAE (Another Arcade Emulator) project.
//
// Copyright (C) 2025-2026 Tim Cottrill
//
// This code is released under the GNU General Public License v3.0
// or (at your option) any later version. See the accompanying
// source files and license text for full details.
// -----------------------------------------------------------------------------

#pragma once

#ifndef _aae_loader_shim_h_
#define _aae_loader_shim_h_

#include <string>
#include <stdbool.h>
#include "sys_fileio.h" // Inherit generic functionality

// =============================================================
// Compatibility Layer
// Maps legacy AAE function calls to sys_fileio implementations
// =============================================================

inline unsigned int get_last_file_size() { return (unsigned int)getLastFileSize(); }
inline unsigned int get_last_zip_file_size() { return (unsigned int)getLastZSize(); }
inline int get_last_crc() { return (int)getLastZCrc(); }

inline unsigned char* load_file(const char* filename) { return loadFile(filename); }
inline int save_file(const char* filename, const unsigned char* buf, int size) { return saveFile(filename, buf, size); }
inline unsigned char* load_zip_file(const char* archname, const char* filename) { return loadZip(archname, filename); }
inline bool save_zip_file(const char* archname, const char* filename, const unsigned char* data) { return saveZip(archname, filename, data); }

// Check if file exists (Wraps sys_fileio or std::filesystem logic)
bool file_exists(const std::string& filename);
bool file_exists(const char* filename);

// =============================================================
// AAE Specific Functionality
// =============================================================

// RomModule structure defined in aae_mame_driver.h, assumed valid here
struct RomModule;

// Loads all ROMs defined in a RomModule list from a ZIP archive
int load_roms(const char* archname, const struct RomModule* p);

// Saves a char buffer to disk as text (legacy wrapper)
int save_file_char(const char* filename, const char* buf, int size);

// Hiscore / NVRAM Specifics
int load_hi_aae(int start, int size, int image);
int save_hi_aae(int start, int size, int image);

// Verification Logic
int verify_rom(const char* archname, const struct RomModule* p, int romnum);
int verify_sample(const char** p, int num);

// Batch Sample Loader - loads all samples from a driver's game_samples list
void load_samples_batch(const char* const* sample_list);

// Load a single sample. Tries the zip (zip_path/zip_entry) first, then the loose
// disk file (disk_path). Returns the mixer sample id, or -1 on failure.
int load_sample_core(const std::string& zip_path, const std::string& zip_entry, const std::string& disk_path);

// -----------------------------------------------------------------------------
// load_ambient_samples
// Loads the 3 optional ambient audio files (flyback, psnoise, hiss) from
// "samples\aae.zip" (or loose files under "samples\aae\"). These are AAE's
// own ambient effect files and are separate from any game's sample set.
//
// Because load_sample_from_buffer assigns sequential IDs via ++sound_id,
// these samples will get IDs AFTER whatever the current game loaded.
// They are looked up by name ("flyback.wav", "psnoise.wav", "hiss.wav")
// via nameToNum() -- never by hardcoded index -- so the order and count
// of game samples does not matter.
//
// Safe to call even if aae.zip is missing; individual missing files are
// logged but do not set have_error.
// -----------------------------------------------------------------------------
void load_ambient_samples();

#endif
