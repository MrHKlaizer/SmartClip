/*
 *  Payload.h — формат «начинки» однофайлового установщика
 *  ─────────────────────────────────────────────────────────
 *  SmartClip_Installer_vX.Y.Z.exe устроен так:
 *
 *      [ Installer.exe ][ сжатые файлы программы ][ PayloadFooter ]
 *
 *  Windows запускает exe как обычно (лишние данные в конце ей не мешают),
 *  а установщик при старте читает свой же файл с конца: находит footer,
 *  по нему — сжатый блок, распаковывает и раскладывает файлы.
 *
 *  Несжатый блок — подряд идущие записи:
 *      uint32  длина пути в байтах
 *      char[]  путь в UTF-8, относительный, через '/'  ("platforms/qwindows.dll")
 *      uint64  размер файла
 *      byte[]  содержимое файла
 *
 *  Сжатие — LZMS из Windows Compression API (cabinet.dll, есть в Windows 8+),
 *  поэтому никаких сторонних библиотек ни в упаковщике, ни в установщике.
 */
#pragma once
#include <cstdint>
#include <cstring>

#pragma pack(push, 1)
struct PayloadFooter {
    char     magic[8];     // "SCPAYLD1"
    uint64_t stubSize;     // Размер самого Installer.exe (смещение начала данных)
    uint64_t packedSize;   // Размер сжатого блока
    uint64_t rawSize;      // Размер блока после распаковки
};
#pragma pack(pop)

static const char PAYLOAD_MAGIC[8] = { 'S','C','P','A','Y','L','D','1' };

inline bool payloadMagicOk(const PayloadFooter &f) {
    return std::memcmp(f.magic, PAYLOAD_MAGIC, sizeof(PAYLOAD_MAGIC)) == 0;
}
