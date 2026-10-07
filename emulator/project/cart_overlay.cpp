// -----------------------------------------------------------------------------
// cart_overlay.cpp - see cart_overlay.h.
// -----------------------------------------------------------------------------
#include "cart_overlay.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

// Read title lines starting at 'p' (the first line's height byte). Returns
// false if a line is unterminated, too long, or not printable text.
static bool read_title_lines(const uint8_t* rom, size_t size, size_t p,
                             std::vector<std::string>& lines)
{
    while (p < size && rom[p] != 0x00 && lines.size() < 8) {
        p += 4;                                    // height, width, y, x
        std::string text;
        while (p < size && rom[p] != 0x80) {
            if (rom[p] < 0x20 || rom[p] > 0x7E || text.size() > 64) return false;
            text += (char)rom[p++];
        }
        if (p >= size) return false;
        lines.push_back(text);
        ++p;                                       // skip $80
    }
    return p < size;                               // reached the $00 terminator
}

std::vector<std::string> cart_title_lines(const uint8_t* rom, size_t size)
{
    std::vector<std::string> lines;
    if (!rom || size < 8) return lines;

    static const char kCopyright[] = "g GCE";
    if (std::equal(kCopyright, kCopyright + 5, rom)) {
        size_t end = 5;
        while (end < size && end < 32 && rom[end] != 0x80) ++end;
        if (end >= size || rom[end] != 0x80) return {};
        if (!read_title_lines(rom, size, end + 1 + 2, lines)) lines.clear();   // + music ptr
    } else if (!read_title_lines(rom, size, 2, lines)) {
        lines.clear();                             // BIOS-style: music ptr, then lines
    }

    // Lines of only spaces carry no name.
    std::vector<std::string> named;
    for (auto& l : lines)
        if (!cart_normalize(l).empty()) named.push_back(l);
    return named;
}

std::string cart_normalize(const std::string& s)
{
    std::string out;
    for (unsigned char c : s)
        if (std::isalnum(c)) out += (char)std::tolower(c);
    return out;
}

std::string cart_crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08X", crc ^ 0xFFFFFFFFu);
    return buf;
}

// aliases.ini: "[crc32]" and "[title]" sections of "key = artwork name" lines,
// ';' or '#' comments. Keys come back upper-cased (crc32) or normalized (title).
struct Aliases { std::map<std::string, std::string> crc, title; };

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static Aliases load_aliases(const fs::path& file)
{
    Aliases al;
    std::ifstream in(file);
    std::string line, section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = cart_normalize(line);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) continue;
        if (section == "crc32") {
            for (auto& c : key) c = (char)std::toupper((unsigned char)c);
            al.crc[key] = value;
        } else if (section == "title") {
            al.title[cart_normalize(key)] = value;
        }
    }
    return al;
}

std::string cart_find_overlay(const uint8_t* rom, size_t size, const std::string& rom_path,
                              const std::string& artwork_dir, std::string* why)
{
    auto hit = [&](const fs::path& p, const std::string& reason) {
        if (why) *why = reason;
        return p.u8string();
    };
    const fs::path dir = fs::u8path(artwork_dir);
    const fs::path rom_file = fs::u8path(rom_path);
    std::error_code ec;

    // 1. Filename overrides.
    fs::path p = dir / (rom_file.stem().u8string() + ".png");
    if (fs::is_regular_file(p, ec)) return hit(p, "rom filename");
    p = rom_file; p.replace_extension(".png");
    if (fs::is_regular_file(p, ec)) return hit(p, "png beside rom");

    // Index the artwork folder by normalized name.
    std::map<std::string, fs::path> art;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string ext = e.path().extension().u8string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".png") art.emplace(cart_normalize(e.path().stem().u8string()), e.path());
    }
    auto art_named = [&](const std::string& name) -> fs::path {
        auto it = art.find(cart_normalize(name));
        return it == art.end() ? fs::path() : it->second;
    };

    const Aliases al = load_aliases(dir / "aliases.ini");

    // 2. CRC32 alias.
    const std::string crc = cart_crc32(rom, size);
    auto c = al.crc.find(crc);
    if (c != al.crc.end() && !(p = art_named(c->second)).empty())
        return hit(p, "crc32 alias " + crc);

    // 3./4. Title alias, then title match: whole title, then first line.
    const std::vector<std::string> lines = cart_title_lines(rom, size);
    if (lines.empty()) return {};
    std::string whole;
    for (auto& l : lines) whole += l;
    for (const std::string& key : { cart_normalize(whole), cart_normalize(lines[0]) }) {
        auto t = al.title.find(key);
        if (t != al.title.end() && !(p = art_named(t->second)).empty())
            return hit(p, "title alias '" + key + "'");
    }
    for (const std::string& key : { cart_normalize(whole), cart_normalize(lines[0]) }) {
        auto a = art.find(key);
        if (a != art.end()) return hit(a->second, "title '" + key + "'");
    }
    return {};
}
