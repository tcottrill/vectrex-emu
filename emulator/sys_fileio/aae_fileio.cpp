// -----------------------------------------------------------------------------
// Game Engine Alpha - Generic Module
// Generic component or utility file for the Game Engine Alpha project. This
// file may contain helpers, shared utilities, or subsystems that integrate
// seamlessly with the engine's rendering, audio, and gameplay frameworks.
//
// Integration:
//   This library is part of the **Game Engine Alpha** project and is tightly
//   integrated with its texture management, logging, and math utility systems.
//
// Usage:
//   Include this module where needed. It is designed to work as a building block
//   for engine subsystems such as rendering, input, audio, or game logic.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// -----------------------------------------------------------------------------

// THIS HAD BEEN STRIPPED FOR THIS PROGRAM, DO NOT USE

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <memory>
#include <filesystem>
#include <cstring>

#include "aae_fileio.h"
#include "sys_log.h"
#include "miniz.h"

#include "memory.h"
#include "path_helper.h"

#include "iniFile.h"
#include "mixer.h"

#define DEBUG_LOG 1

#ifdef DEBUG_LOG
#define DLOG(msg, ...) LOG_INFO(msg, ##__VA_ARGS__)
#else
#define DLOG(msg, ...)
#endif



// Wrapper for std::filesystem or sys_fileio check
bool file_exists(const std::string& filename)
{
    // You could also just call fileExistsReadable(filename.c_str());
    return std::filesystem::exists(filename) && std::filesystem::is_regular_file(filename);
}

bool file_exists(const char* filename)
{
    return file_exists(std::string(filename));
}

// Wrapper for text saving
int save_file_char(const char* filename, const char* buf, int size) {
    // Cast char* to unsigned char* and let sys_fileio handle it
    return saveFile(filename, (const unsigned char*)buf, size) ? 1 : 0;
}


// --- SAMPLE LOADER ---

// Custom Deleter for unique_ptr using free
struct MallocDeleter {
    void operator()(uint8_t* p) const { free(p); }
};

int load_sample_core(const std::string& zip_path, const std::string& zip_entry, const std::string& disk_path)
{
    std::unique_ptr<uint8_t, MallocDeleter> buffer;
    size_t size = 0;
    bool loaded_from_zip = false;

    // A: Try loading from Zip Archive first via sys_fileio
    if (!zip_path.empty()) {
        uint8_t* zip_data = loadZip(zip_path.c_str(), zip_entry.c_str());
        if (zip_data) {
            buffer.reset(zip_data);
            size = getLastZSize();
            loaded_from_zip = true;
        }
    }

    // B: Fallback to direct file load via sys_fileio
    if (!buffer) {
        uint8_t* file_data = loadFile(disk_path.c_str());
        if (file_data) {
            buffer.reset(file_data);
            size = getLastFileSize();
        }
    }

    // C: Validation
    if (!buffer || size == 0) {
        LOG_ERROR("Failed to load sample: '%s' (Checked Zip: '%s' and Disk: '%s')",
            zip_entry.c_str(), zip_path.c_str(), disk_path.c_str());
        return -1;
    }

    // D: Submit to Mixer
    int sample_id = load_sample_from_buffer(buffer.get(), size, zip_entry.c_str(), true);

    if (sample_id >= 0) {
        LOG_INFO("Loaded sample ID %d: %s %s",
            sample_id, zip_entry.c_str(), (loaded_from_zip ? "[Zip]" : "[File]"));
    }

    return sample_id;
}

void load_samples_batch(const char* const* sample_list)
{
    if (!sample_list || !sample_list[0]) return;

    std::string archiveName = sample_list[0];
    std::string fullZipPath = "samples\\" + archiveName;

    std::string subFolderName = archiveName;
    size_t lastDot = subFolderName.find_last_of('.');
    if (lastDot != std::string::npos) {
        subFolderName = subFolderName.substr(0, lastDot);
    }

    LOG_INFO("Batch loading samples. Archive: '%s', Fallback Dir: 'samples\\%s\\'",
        fullZipPath.c_str(), subFolderName.c_str());

    int i = 1;
    while (sample_list[i] != nullptr) {
        const char* filename = sample_list[i];
        if (strcmp(filename, "NULL") == 0) break;

        std::string entryName = filename;
        std::string fullDiskPath = "samples\\" + subFolderName + "\\" + entryName;

        load_sample_core(fullZipPath, entryName, fullDiskPath);
        i++;
    }
}

// -----------------------------------------------------------------------------
// load_ambient_samples
//
// Loads the 3 optional AAE ambient audio files from "samples\aae.zip"
// (with loose-file fallback to "samples\aae\").
//
// The ambient files are:
//   - flyback.wav   (CRT horizontal flyback chatter)
//   - psnoise.wav   (power supply hum / buzz)
//   - hiss.wav      (background static / tape hiss)
//
// These are loaded into the mixer's sample registry just like any game
// sample. Because load_sample_from_buffer() assigns sequential IDs via
// ++sound_id, the ambient samples will always get IDs AFTER whatever the
// current game has loaded. They are looked up by NAME (via nameToNum)
// rather than by index, so the number of game samples does not matter.
//
// This function is safe to call even if aae.zip does not exist or if
// individual files are missing -- each file is loaded independently and
// a missing file is logged as INFO, not treated as a fatal error.
// -----------------------------------------------------------------------------
void load_ambient_samples()
{
    // The ambient sample filenames, loaded from "samples\aae.zip"
    static const char* ambient_files[] = {
        "flyback.wav",
        "psnoise.wav",
        "hiss.wav",
        nullptr
    };

    const std::string zipPath = "samples\\aae.zip";
    const std::string diskBase = "samples\\aae\\";

    LOG_INFO("Loading ambient samples from '%s' (fallback: '%s')", zipPath.c_str(), diskBase.c_str());

    int loaded_count = 0;

    for (int i = 0; ambient_files[i] != nullptr; ++i)
    {
        const std::string entryName = ambient_files[i];
        const std::string diskPath = diskBase + entryName;

        int id = load_sample_core(zipPath, entryName, diskPath);

        if (id >= 0) {
            LOG_INFO("Ambient sample '%s' loaded as sample ID %d", entryName.c_str(), id);
            loaded_count++;
        }
        else {
            // Not fatal -- ambient audio is optional
            LOG_INFO("Ambient sample '%s' not found (optional, skipping)", entryName.c_str());
        }
    }

    LOG_INFO("Ambient sample loading complete: %d of 3 loaded", loaded_count);
}
