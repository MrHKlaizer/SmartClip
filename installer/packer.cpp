/*
 *  SmartClipPacker — собирает однофайловый установщик
 *  ───────────────────────────────────────────────────
 *  Использование:
 *      SmartClipPacker.exe <Installer.exe> <папка_с_программой> <результат.exe>
 *
 *  Берёт Installer.exe, дописывает в конец сжатое содержимое папки
 *  (SmartClip.exe + Qt DLL + переводы) и footer (см. Payload.h).
 *  Запускается из make_release.bat — руками вызывать не нужно.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <compressapi.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include "Payload.h"

namespace fs = std::filesystem;

static bool readFile(const fs::path &p, std::vector<char> &out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

template <typename T>
static void put(std::vector<char> &buf, const T &v) {
    const char *p = reinterpret_cast<const char *>(&v);
    buf.insert(buf.end(), p, p + sizeof(T));
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 4) {
        std::fwprintf(stderr, L"usage: SmartClipPacker <Installer.exe> <srcDir> <out.exe>\n");
        return 2;
    }
    const fs::path stubPath = argv[1], srcDir = argv[2], outPath = argv[3];

    std::vector<char> stub;
    if (!readFile(stubPath, stub)) {
        std::fwprintf(stderr, L"cannot read %ls\n", stubPath.c_str());
        return 1;
    }

    // ── Собираем несжатый блок ───────────────────────────────────────────────
    std::vector<fs::path> files;
    for (auto &e : fs::recursive_directory_iterator(srcDir))
        if (e.is_regular_file()) files.push_back(e.path());
    std::sort(files.begin(), files.end());

    std::vector<char> raw;
    for (const auto &p : files) {
        std::string rel = fs::relative(p, srcDir).generic_u8string();
        std::vector<char> data;
        if (!readFile(p, data)) {
            std::fwprintf(stderr, L"cannot read %ls\n", p.c_str());
            return 1;
        }
        put<uint32_t>(raw, static_cast<uint32_t>(rel.size()));
        raw.insert(raw.end(), rel.begin(), rel.end());
        put<uint64_t>(raw, static_cast<uint64_t>(data.size()));
        raw.insert(raw.end(), data.begin(), data.end());
    }
    std::wprintf(L"Files: %zu, raw size: %.1f MB\n", files.size(), raw.size() / 1048576.0);

    // ── Сжимаем (LZMS — лучшее сжатие из встроенных в Windows) ───────────────
    COMPRESSOR_HANDLE comp = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &comp)) {
        std::fwprintf(stderr, L"CreateCompressor failed: %lu\n", GetLastError());
        return 1;
    }
    SIZE_T needed = 0;
    Compress(comp, raw.data(), raw.size(), nullptr, 0, &needed);  // Узнаём нужный размер
    std::vector<char> packed(needed);
    SIZE_T packedSize = 0;
    if (!Compress(comp, raw.data(), raw.size(), packed.data(), packed.size(), &packedSize)) {
        std::fwprintf(stderr, L"Compress failed: %lu\n", GetLastError());
        CloseCompressor(comp);
        return 1;
    }
    CloseCompressor(comp);
    std::wprintf(L"Packed size: %.1f MB\n", packedSize / 1048576.0);

    // ── Пишем результат: stub + packed + footer ──────────────────────────────
    PayloadFooter footer{};
    std::memcpy(footer.magic, PAYLOAD_MAGIC, sizeof(PAYLOAD_MAGIC));
    footer.stubSize   = stub.size();
    footer.packedSize = packedSize;
    footer.rawSize    = raw.size();

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    out.write(stub.data(), static_cast<std::streamsize>(stub.size()));
    out.write(packed.data(), static_cast<std::streamsize>(packedSize));
    out.write(reinterpret_cast<const char *>(&footer), sizeof(footer));
    if (!out) {
        std::fwprintf(stderr, L"cannot write %ls\n", outPath.c_str());
        return 1;
    }
    std::wprintf(L"OK: %ls\n", outPath.c_str());
    return 0;
}
