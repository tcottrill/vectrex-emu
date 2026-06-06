/* =============================================================================
 * File: iniFile.h
 * Component: INI configuration file handling (public API)
 *
 * Overview
 * --------
 * Lightweight reader/writer for INI-style configuration files. A single active
 * file is selected with SetIniFile(); values are then read and written by
 * [section]/key with typed accessors that fall back to a supplied default when
 * the entry is absent.
 *
 * API
 * ---
 *   void  SetIniFile(const char* szFileName);
 *       Select the INI file used by every get_/set_ call that follows.
 *
 *   // Typed reads -- return defaultValue when the section/key is missing
 *   int   get_config_int   (section, key, defaultValue);
 *   float get_config_float (section, key, defaultValue);
 *   bool  get_config_bool  (section, key, defaultValue);
 *   char* get_config_string(section, key, defaultValue);   // caller must free()
 *
 *   // Typed writes -- create the section/key as needed
 *   void  set_config_int   (section, key, value);
 *   void  set_config_float (section, key, value);
 *   void  set_config_bool  (section, key, value);
 *   void  set_config_string(section, key, value);
 *
 *   std::string overloads of all of the above are also provided; the
 *   std::string get_config_string overload returns by value (no free()).
 *
 * Notes
 * -----
 *   Booleans are persisted as the text True / False. Some of this code was
 *   written with the assistance of ChatGPT (see iniFile.cpp).
 *
 * ---------------------------------------------------------------------------
 * License: The Unlicense (public domain) -- matches iniFile.cpp.
 *
 *   This is free and unencumbered software released into the public domain.
 *   Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 *   it, in source or binary form, for any purpose, commercial or not, by any
 *   means. The software is provided "AS IS", without warranty of any kind.
 *   For the full text, see <https://unlicense.org/>.
 *
 *   SPDX-License-Identifier: Unlicense
 * ============================================================================= */

#ifndef INI_H
#define INI_H

#include <string>

void SetIniFile(const char* szFileName);

int   get_config_int(const char* szSection, const char* szKey, int iDefaultValue);
float get_config_float(const char* szSection, const char* szKey, float fltDefaultValue);
bool  get_config_bool(const char* szSection, const char* szKey, bool bolDefaultValue);
char* get_config_string(const char* szSection, const char* szKey, const char* szDefaultValue); // Caller must free()

void set_config_int(const char* szSection, const char* szKey, int iValue);
void set_config_float(const char* szSection, const char* szKey, float fltValue);
void set_config_bool(const char* szSection, const char* szKey, bool bolValue);
void set_config_string(const char* szSection, const char* szKey, const char* szValue);

// std string overloads

int   get_config_int(const std::string& section, const std::string& key, int defaultValue);
float get_config_float(const std::string& section, const std::string& key, float defaultValue);
bool  get_config_bool(const std::string& section, const std::string& key, bool defaultValue);
std::string get_config_string(const std::string& section, const std::string& key, const std::string& defaultValue);

void set_config_int(const std::string& section, const std::string& key, int value);
void set_config_float(const std::string& section, const std::string& key, float value);
void set_config_bool(const std::string& section, const std::string& key, bool value);
void set_config_string(const std::string& section, const std::string& key, const std::string& value);
#endif // INI_H