#pragma once
// -----------------------------------------------------------------------------
// cart_overlay - pick the overlay artwork for a cartridge.
//
// Lookup order (first hit wins):
//   1. data/artwork/<rom filename>.png, then <rom path>.png beside the ROM
//      (a filename match is always an explicit override);
//   2. data/artwork/aliases.ini [crc32]: the cart image's CRC32 -> artwork;
//   3. data/artwork/aliases.ini [title]: the title in the cart header -> artwork;
//   4. an artwork file whose name matches the cart's title.
// Titles and artwork names are compared normalized: lower case, letters and
// digits only ("FORTRESS / OF / NARZOD" == "fortressofnarzod"). Steps 3 and 4
// try the whole title first, then its first line only.
//
// No GL here: the result is a path for emulator_load_overlay().
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Title lines from a Vectrex cart header: "g GCE yyyy" $80, a 2-byte music
// pointer, then per line height/width/y/x + text + $80, ended by $00. Images
// with no copyright string (dumps of the BIOS's built-in Mine Storm) are read
// from offset 2 if every line is printable text. Empty when there is no title.
std::vector<std::string> cart_title_lines(const uint8_t* rom, size_t size);

// Lower case, letters and digits only.
std::string cart_normalize(const std::string& s);

// Standard CRC-32 (the checksum ROM sets list), as 8 upper-case hex digits.
std::string cart_crc32(const uint8_t* data, size_t size);

// The overlay image for a cart, or "" if none. rom/size: the cart image;
// rom_path: the file it came from; artwork_dir: e.g. "data/artwork".
// 'why' (optional) receives a short description of the step that matched.
std::string cart_find_overlay(const uint8_t* rom, size_t size, const std::string& rom_path,
                              const std::string& artwork_dir, std::string* why = nullptr);
