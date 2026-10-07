// Tests for cart_overlay (title parsing, CRC32, overlay lookup order).
// With a data directory argument it also surveys every ROM there:
//   cart_overlay_tests.exe ..\..\x64\Release\data
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "cart_overlay.h"

namespace fs = std::filesystem;

static int failures = 0;
static void check(const char* name, bool ok)
{
    std::printf("%-60s %s\n", name, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static std::vector<uint8_t> bytes(std::initializer_list<int> v)
{
    return std::vector<uint8_t>(v.begin(), v.end());
}

// A cart header: "g GCE 1982" $80, music ptr, then one block per line.
static std::vector<uint8_t> cart(std::initializer_list<const char*> lines, bool copyright = true)
{
    std::vector<uint8_t> r;
    if (copyright) { for (const char* c = "g GCE 1982"; *c; ++c) r.push_back((uint8_t)*c); r.push_back(0x80); }
    r.push_back(0xFD); r.push_back(0x0D);                       // music pointer
    for (const char* l : lines) {
        for (int b : { 0xF8, 0x50, 0x20, 0xD0 }) r.push_back((uint8_t)b);
        while (*l) r.push_back((uint8_t)*l++);
        r.push_back(0x80);
    }
    r.push_back(0x00);
    for (int i = 0; i < 16; ++i) r.push_back(0x12);              // code follows
    return r;
}

static void touch(const fs::path& p) { std::ofstream(p) << "x"; }

static void survey(const fs::path& data)
{
    std::printf("\nSurvey of %s\n", (data / "roms").u8string().c_str());
    for (auto& e : fs::directory_iterator(data / "roms")) {
        std::string ext = e.path().extension().u8string();
        for (auto& c : ext) c = (char)tolower((unsigned char)c);
        if (ext != ".vec" && ext != ".bin" && ext != ".gam") continue;
        std::ifstream in(e.path(), std::ios::binary);
        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), {});
        if (rom.size() > 0x8000) rom.resize(0x8000);             // what the emulator loads
        std::string why, title;
        for (auto& l : cart_title_lines(rom.data(), rom.size())) title += (title.empty() ? "" : " / ") + l;
        std::string art = cart_find_overlay(rom.data(), rom.size(), e.path().u8string(),
                                            (data / "artwork").u8string(), &why);
        std::printf("  %-52s %s  %-28s -> %s%s%s\n", e.path().filename().u8string().c_str(),
                    cart_crc32(rom.data(), rom.size()).c_str(), ("\"" + title + "\"").c_str(),
                    art.empty() ? "(none)" : fs::u8path(art).filename().u8string().c_str(),
                    art.empty() ? "" : "  via ", why.c_str());
    }
}

int main(int argc, char** argv)
{
    // --- Title parsing ---
    {
        auto r = cart({ "FORTRESS", "OF", "NARZOD" });
        auto t = cart_title_lines(r.data(), r.size());
        check("multi-line title", t.size() == 3 && t[0] == "FORTRESS" && t[2] == "NARZOD");
    }
    {
        auto r = cart({ "MINE", "STORM" }, false);   // BIOS-style: no copyright
        auto t = cart_title_lines(r.data(), r.size());
        check("BIOS-style header (no copyright) still read", t.size() == 2 && t[1] == "STORM");
    }
    {
        auto r = cart({ " ", "RIP-OFF", "   " });
        auto t = cart_title_lines(r.data(), r.size());
        check("blank lines dropped", t.size() == 1 && t[0] == "RIP-OFF");
    }
    {
        auto r = bytes({ 0x12, 0x34, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 });
        check("no header -> no title", cart_title_lines(r.data(), r.size()).empty());
    }
    {
        auto r = cart({ "OK" });
        r.resize(r.size() - 18);                       // cut inside the header
        check("truncated header -> no title", cart_title_lines(r.data(), r.size()).empty());
    }
    check("normalize", cart_normalize("Star Trek - The Game!") == "startrekthegame");
    {
        const char* s = "123456789";                   // the standard CRC-32 check value
        check("crc32 check value CBF43926",
              cart_crc32((const uint8_t*)s, strlen(s)) == "CBF43926");
    }

    // --- Lookup order, in a scratch artwork folder ---
    const fs::path tmp = fs::temp_directory_path() / "vectrex_cart_overlay_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "art");
    fs::create_directories(tmp / "roms");
    touch(tmp / "art" / "Fortress_of_Narzod.png");
    touch(tmp / "art" / "ripoff.png");
    touch(tmp / "art" / "special.png");
    touch(tmp / "art" / "byname.png");
    const std::string art = (tmp / "art").u8string();
    std::string why;

    auto narzod = cart({ "FORTRESS", "OF", "NARZOD" });
    std::string got = cart_find_overlay(narzod.data(), narzod.size(), (tmp / "roms" / "x.vec").u8string(), art, &why);
    check("title match (whole title)", fs::u8path(got).filename() == "Fortress_of_Narzod.png");

    auto ripoff = cart({ "RIP-OFF", "0  " });
    got = cart_find_overlay(ripoff.data(), ripoff.size(), (tmp / "roms" / "y.vec").u8string(), art, &why);
    check("title match (first line)", fs::u8path(got).filename() == "ripoff.png");

    got = cart_find_overlay(narzod.data(), narzod.size(), (tmp / "roms" / "byname.vec").u8string(), art, &why);
    check("rom filename beats title", fs::u8path(got).filename() == "byname.png");

    std::ofstream(tmp / "art" / "aliases.ini")
        << "; test\n[title]\nRIP-OFF = special\n[crc32]\n"
        << cart_crc32(narzod.data(), narzod.size()) << " = special\n";
    got = cart_find_overlay(ripoff.data(), ripoff.size(), (tmp / "roms" / "y.vec").u8string(), art, &why);
    check("title alias beats title match", fs::u8path(got).filename() == "special.png");
    got = cart_find_overlay(narzod.data(), narzod.size(), (tmp / "roms" / "x.vec").u8string(), art, &why);
    check("crc32 alias beats title", fs::u8path(got).filename() == "special.png");

    auto unknown = cart({ "NOTHING HERE" });
    got = cart_find_overlay(unknown.data(), unknown.size(), (tmp / "roms" / "z.vec").u8string(), art, &why);
    check("no match -> empty", got.empty());
    fs::remove_all(tmp);

    if (argc > 1) survey(fs::u8path(argv[1]));

    std::printf(failures ? "\nSOME TESTS FAILED (%d)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
