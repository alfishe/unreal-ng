// WEATHER_GET's text work (zifiweather.h): the three HTTP answers parsed into the 90-byte record, zifi.ini text
// to UTF-8, the place to CP866, the request paths. Expected values follow the firmware's own host test
// (ZiFi-ESP32-S3-Zero 2e5ba83 tests/test_weather_parse.py) on answers shaped like the services' real ones.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "emulator/io/serial/esp/zifiweather.h"

namespace
{
using Bytes = std::vector<uint8_t>;

// Open-Meteo, Rome, timezone=auto (UTC+2), timeformat=unixtime; local midnight 2025-10-04 = 1759528800
const char* const kForecast =
    R"({"latitude":41.875,"longitude":12.5,"generationtime_ms":0.1,"utc_offset_seconds":7200,"timezone":"Europe/Rome",)"
    R"("current_units":{"time":"unixtime"},"current":{"time":1759577400,"interval":900,"temperature_2m":22.5,)"
    R"("weather_code":3,"is_day":1,"wind_speed_10m":7.6,"precipitation":0.0,"surface_pressure":1013.2},)"
    R"("daily":{"time":[1759528800,1759615200,1759701600,1759788000,1759874400,1759960800],)"
    R"("weather_code":[3,61,2,1,0,80],"temperature_2m_max":[24.1,22.0,21.5,23.0,24.4,19.9],)"
    R"("temperature_2m_min":[14.2,15.0,13.1,12.9,13.4,12.0],"precipitation_sum":[0.0,4.25,0.0,0.0,0.0,12.0],)"
    R"("sunrise":[1759555200,1759641660,1759728120,1759814580,1759901040,1759987500],)"
    R"("sunset":[1759597500,1759683840,1759770180,1759856520,1759942860,1760029200]}})";

std::string Cp866(std::initializer_list<int> bytes)
{
    std::string s;
    for (int b : bytes)
        s.push_back(static_cast<char>(b));
    return s;
}
}  // namespace

TEST(ZiFiWeather_Test, ForecastFillsTheRecord)
{
    Bytes record;
    std::string error;
    ASSERT_TRUE(zifiweather::ParseOpenMeteo(kForecast, "Roma", record, error)) << error;
    ASSERT_EQ(record.size(), 90u);
    EXPECT_EQ(record[0], 1) << "status";
    EXPECT_EQ(record[1], 1) << "format version";
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(record.data() + 2)), "Roma");
    EXPECT_EQ(static_cast<int8_t>(record[26]), 23) << "22.5 rounds away from zero (lround)";
    EXPECT_EQ(record[27], 3);
    EXPECT_EQ(record[28], 1);
    EXPECT_EQ(record[29] | (record[30] << 8), 76) << "wind x10";
    EXPECT_EQ(record[31] | (record[32] << 8), 0);
    EXPECT_EQ(record[33] | (record[34] << 8), 760) << "1013.2 hPa in mm Hg";
    EXPECT_EQ(record[35], 7);
    EXPECT_EQ(record[36], 20) << "sunrise 07:20 local";
    EXPECT_EQ(record[37], 19);
    EXPECT_EQ(record[38], 5) << "sunset 19:05";
    EXPECT_EQ(record[39], 13);
    EXPECT_EQ(record[40], 30) << "data time 13:30";
    ASSERT_EQ(record[41], 6);
    const int days[6][8] = {{4, 10, 5, 3, 14, 24, 0, 0},  {5, 10, 6, 61, 15, 22, 43, 0}, {6, 10, 0, 2, 13, 22, 0, 0},
                            {7, 10, 1, 1, 13, 23, 0, 0},  {8, 10, 2, 0, 13, 24, 0, 0},   {9, 10, 3, 80, 12, 20, 120, 0}};
    for (int d = 0; d < 6; ++d)
    {
        for (int i = 0; i < 8; ++i)
            EXPECT_EQ(record[42 + d * 8 + i], static_cast<uint8_t>(days[d][i])) << "day " << d << " byte " << i;
    }
}

TEST(ZiFiWeather_Test, ForecastRefusesWhatItCannotRead)
{
    Bytes record;
    std::string error;
    EXPECT_FALSE(zifiweather::ParseOpenMeteo(R"({"current":{}})", "", record, error));
    EXPECT_EQ(error, "meteo: no utc offset");
    EXPECT_FALSE(zifiweather::ParseOpenMeteo(R"({"utc_offset_seconds":0,"current":{"time":1}})", "", record, error));
    EXPECT_EQ(error, "meteo: no current/daily");
    EXPECT_FALSE(zifiweather::ParseOpenMeteo(R"({"utc_offset_seconds":0,"current":{"time":1},"daily":{}})", "", record, error));
    EXPECT_EQ(error, "meteo: current fields");
}

TEST(ZiFiWeather_Test, GeocoderTakesTheFirstPlace)
{
    zifiweather::GeoResult geo;
    bool notFound = true;
    std::string error;
    ASSERT_TRUE(zifiweather::ParseCitySearch(
        R"({"results":[{"id":3169070,"name":"Rome","latitude":41.89193,"longitude":12.51133,"country_code":"IT"},)"
        R"({"id":1,"name":"Rome","latitude":34.25704,"longitude":-85.16467}],"generationtime_ms":0.6})",
        geo, notFound, error));
    EXPECT_EQ(geo.place, "Rome");
    EXPECT_FLOAT_EQ(geo.latitude, 41.89193f);
    EXPECT_FLOAT_EQ(geo.longitude, 12.51133f);
    ASSERT_TRUE(zifiweather::ParseCitySearch(R"({"results":[{"name":"Рим","latitude":41.9,"longitude":12.5}]})",
                                             geo, notFound, error));
    EXPECT_EQ(geo.place, "\xD0\xA0\xD0\xB8\xD0\xBC") << "\\u escapes to UTF-8 (Рим)";

    EXPECT_FALSE(zifiweather::ParseCitySearch(R"({"generationtime_ms":0.4})", geo, notFound, error));
    EXPECT_TRUE(notFound) << "no results: the place does not exist";
    EXPECT_EQ(error, "city: not found");
    EXPECT_FALSE(zifiweather::ParseCitySearch(R"({"results":[]})", geo, notFound, error));
    EXPECT_TRUE(notFound);
    EXPECT_FALSE(zifiweather::ParseCitySearch("[]", geo, notFound, error));
    EXPECT_FALSE(notFound) << "a broken answer may work on the next try";
    EXPECT_EQ(error, "city: broken json");
    EXPECT_FALSE(zifiweather::ParseCitySearch(R"({"results":[{"name":"x"}]})", geo, notFound, error));
    EXPECT_EQ(error, "city: no coordinates");
}

TEST(ZiFiWeather_Test, ZippopotamTakesTheLastPlace)
{
    zifiweather::GeoResult geo;
    std::string error;
    ASSERT_TRUE(zifiweather::ParseZippopotam(
        R"({"post code":"00144","country":"Italy","places":[{"place name":"Roma Eur","longitude":"12.47","latitude":"41.83"},)"
        R"({"place name":"Roma","longitude":"12.4839","latitude":"41.8947"}]})",
        geo, error));
    EXPECT_EQ(geo.place, "Roma");
    EXPECT_FLOAT_EQ(geo.latitude, 41.8947f) << "quoted numbers";
    EXPECT_FALSE(zifiweather::ParseZippopotam("{}", geo, error));
    EXPECT_EQ(error, "zip: no places");
}

TEST(ZiFiWeather_Test, IniTextBecomesUtf8)
{
    EXPECT_EQ(zifiweather::IniTextToUtf8("Kyiv"), "Kyiv");
    const std::string rim = "\xD0\xA0\xD0\xB8\xD0\xBC";   // Рим
    EXPECT_EQ(zifiweather::IniTextToUtf8(rim), rim) << "UTF-8 stays";
    EXPECT_EQ(zifiweather::IniTextToUtf8(Cp866({0x90, 0xA8, 0xAC})), rim) << "CP866 (the WC editor)";
    EXPECT_EQ(zifiweather::IniTextToUtf8(Cp866({0xD0, 0xE8, 0xEC})), rim) << "CP1251 (Notepad ANSI)";
    const std::string kyiv = "\xD0\x9A\xD0\xB8\xD1\x97\xD0\xB2";   // Київ
    EXPECT_EQ(zifiweather::IniTextToUtf8(Cp866({0xCA, 0xE8, 0xBF, 0xE2})), kyiv) << "CP1251 with ї";
    EXPECT_EQ(zifiweather::IniTextToUtf8(Cp866({0x8A, 0xA8, 0xF5, 0xA2})), kyiv) << "CP866 with ї";
}

TEST(ZiFiWeather_Test, PlaceBecomesCp866)
{
    EXPECT_EQ(zifiweather::Utf8ToCp866("Z\xC3\xBCrich Gr\xC3\xA9oux", 64), "Zurich Greoux") << "accents to the base letter";
    EXPECT_EQ(zifiweather::Utf8ToCp866("\xD0\xA0\xD0\xB8\xD0\xBC 15\xC2\xB0", 64), Cp866({0x90, 0xA8, 0xAC, ' ', '1', '5', 0xF8}));
    EXPECT_EQ(zifiweather::Utf8ToCp866("\xD0\x86\xD1\x96\xD2\x90 \xE2\x80\x99", 64), Cp866({'I', 'i', 0x83, ' ', '\''}))
        << "І і as Latin I i, Ґ as Г, the apostrophe in ASCII";
    EXPECT_EQ(zifiweather::Utf8ToCp866("Citta di Castello Umbria", 24).size(), 23u) << "24 bytes with the NUL";
}

TEST(ZiFiWeather_Test, RequestPaths)
{
    std::string path;
    ASSERT_TRUE(zifiweather::CityPath("Rome", "IT", path));
    EXPECT_EQ(path, "/v1/search?name=Rome&count=1&language=en&format=json&countryCode=IT");
    ASSERT_TRUE(zifiweather::CityPath(Cp866({0x90, 0xA8, 0xAC}), "", path));
    EXPECT_EQ(path, "/v1/search?name=%D0%A0%D0%B8%D0%BC&count=1&language=ru&format=json") << "Cyrillic asks in Russian";
    ASSERT_TRUE(zifiweather::CityPath("New York", "", path));
    EXPECT_EQ(path, "/v1/search?name=New%20York&count=1&language=en&format=json");
    ASSERT_TRUE(zifiweather::ZipPath("IT", "00144", path));
    EXPECT_EQ(path, "/IT/00144");
    EXPECT_FALSE(zifiweather::ZipPath("IT", std::string(100, '1'), path)) << "96-byte buffer";
    EXPECT_EQ(zifiweather::ForecastPath(41.89193f, -12.5f).substr(0, 51),
              "/v1/forecast?latitude=41.8919&longitude=-12.5000&cu");
}
