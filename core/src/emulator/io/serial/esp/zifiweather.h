#pragma once

/// @file zifiweather.h
/// @brief WEATHER_GET of the ZIFI-NATIVE S3 firmware: the record the Wild Commander screen savers show
/// (WEATHER.WMF, WEATHER2.WMF) and the text work behind it - the three HTTP answers parsed, zifi.ini text to UTF-8,
/// the place name to CP866. A port of ZiFi-ESP32-S3-Zero 2e5ba83 src/weather_parse.cpp and the request paths of
/// src/weather_service.cpp (docs/PROTOCOL.md "WEATHER_GET"); the module (ZiFiNativeModule) runs the HTTP part.
///
/// The place: zifi.ini `city:` (any spelling the Open-Meteo geocoder knows; Cyrillic is looked up with
/// language=ru), optional `country:` (ISO code); without `city:`, `country:` + `zip:` through api.zippopotam.us
/// (the last place of the list). Then the forecast from api.open-meteo.com, all over HTTP port 80.
///
/// The A4 answer is 90 bytes, little-endian:
///   +0 status (1), +1 format version (1), +2 place (CP866, NUL-ended, 24 bytes), +26 temperature (signed C),
///   +27 WMO code, +28 day (1) / night (0), +29 wind km/h x10, +31 precipitation mm/h x10, +33 pressure mm Hg,
///   +35 sunrise h m, +37 sunset h m, +39 local time of the data h m, +41 day count (6),
///   +42 six days of 8 bytes: day, month, weekday (0 = Monday), WMO code, min C, max C, precipitation mm x10 u16
/// On failure the answer is the short [0][1], after an EE report ("weather:city: not found", ...).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zifiweather
{

constexpr uint8_t kRecordVersion = 1;
constexpr size_t kPlaceLength = 24;
constexpr size_t kMaxDays = 6;
constexpr size_t kDaySize = 8;

// Field offsets (weather_parse.hpp WeatherRecordLayout)
constexpr size_t kStatus = 0, kVersion = 1, kPlace = 2, kTemp = 26, kCode = 27, kIsDay = 28, kWind10 = 29,
                 kPrecip10 = 31, kPressure = 33, kSunrise = 35, kSunset = 37, kDataTime = 39, kDayCount = 41,
                 kDays = 42;
constexpr size_t kRecordSize = kDays + kDaySize * kMaxDays;   // 90

// The services (weather_service.cpp)
constexpr const char* kCityHost = "geocoding-api.open-meteo.com";
constexpr const char* kZipHost = "api.zippopotam.us";
constexpr const char* kMeteoHost = "api.open-meteo.com";
constexpr uint16_t kHttpPort = 80;
constexpr size_t kBodyCapacity = 4096;          // a body must stay below it ("reply too long")
constexpr uint64_t kBodyTimeoutUs = 15000000;   // kBodyTimeoutMs: no body byte for 15 s
constexpr int kHttpAttempts = 3;                // a 5xx / 429 / dropped request: again after 1 s, then 2 s
constexpr uint64_t kBudgetUs = 30000000;        // kWeatherBudgetMs: no new attempt after 30 s

/// A place the geocoder found
struct GeoResult
{
    float latitude = 0.0f;
    float longitude = 0.0f;
    std::string place;   ///< UTF-8, at most 63 bytes (char place[64])
};

/// zifi.ini text (UTF-8, CP866 from the Wild Commander editor or CP1251 from Notepad "ANSI") to UTF-8
std::string IniTextToUtf8(const std::string& text);
/// UTF-8 to CP866 (Cyrillic, the Ukrainian / Belarusian letters CP866 has, accented Latin to its base letter),
/// at most `capacity - 1` bytes
std::string Utf8ToCp866(const std::string& text, size_t capacity);

/// The geocoder path for `city` (zifi.ini text) and optional `country`; false when it does not fit (384 bytes)
bool CityPath(const std::string& city, const std::string& country, std::string& path);
/// The zippopotam path /<country>/<zip>; false when it does not fit (96 bytes)
bool ZipPath(const std::string& country, const std::string& zip, std::string& path);
/// The forecast path for the coordinates
std::string ForecastPath(float latitude, float longitude);

/// The geocoder's answer: the first place. `notFound` = no such name (the service remembers it)
bool ParseCitySearch(const std::string& json, GeoResult& out, bool& notFound, std::string& error);
/// zippopotam's answer: the last place of the list
bool ParseZippopotam(const std::string& json, GeoResult& out, std::string& error);
/// The forecast into the 90-byte record (`record` resized); `placeUtf8` goes to CP866 here, once
bool ParseOpenMeteo(const std::string& json, const std::string& placeUtf8, std::vector<uint8_t>& record,
                    std::string& error);

}  // namespace zifiweather
