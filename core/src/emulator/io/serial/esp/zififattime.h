#pragma once

/// @file zififattime.h
/// @brief FAT date / time stamps and the FTP time-val (RFC 3659), as the ZIFI-NATIVE S3 firmware converts them
/// (ZiFi-ESP32-S3-Zero 2e5ba83 include/zifi/fat_time.hpp, src/fat_time.cpp). FAT stores local time; MDTM, MFMT and
/// MLSD carry UTC, so the zone from zifi.ini (time:) converts both ways.

#include <cstdint>

namespace zififat
{

struct Stamp
{
    uint16_t date = 0;
    uint16_t time = 0;
    bool Known() const { return date != 0; }
};

int64_t CivilToUnix(int year, int month, int day, int hour, int minute, int second);
void UnixToCivil(int64_t unixSeconds, int& year, int& month, int& day, int& hour, int& minute, int& second);
bool StampToCivil(Stamp stamp, int& year, int& month, int& day, int& hour, int& minute, int& second);
bool StampToUnix(Stamp stamp, int32_t timezoneSeconds, int64_t& unixSeconds);
bool UnixToStamp(int64_t unixSeconds, int32_t timezoneSeconds, Stamp& stamp);
/// YYYYMMDDhhmmss[.fff]; `end` gets the first character after it
bool ParseFtpTimeVal(const char* text, int64_t& unixSeconds, const char** end = nullptr);
/// 14 characters + NUL into `output` (15 bytes)
void FormatFtpTimeVal(int64_t unixSeconds, char* output);

}  // namespace zififat
