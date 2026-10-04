// [NETWORK] ComPort= values (network adapters TDD §8)

#include <gtest/gtest.h>

#include "emulator/io/serial/comportspec.h"

TEST(ComPortSpec_Test, NoneLoopbackAndEmpty)
{
    ComPortSpec s;
    std::string error;
    EXPECT_TRUE(ComPortSpec::Parse("", s, error));
    EXPECT_EQ(s.kind, ComPortSpec::Kind::None);
    EXPECT_TRUE(ComPortSpec::Parse(" none ", s, error));
    EXPECT_EQ(s.kind, ComPortSpec::Kind::None);
    EXPECT_TRUE(ComPortSpec::Parse("Loopback", s, error));
    EXPECT_EQ(s.kind, ComPortSpec::Kind::Loopback);
    EXPECT_EQ(s.ToString(), "LOOPBACK");
    EXPECT_TRUE(ComPortSpec::Parse(" plug ", s, error));
    EXPECT_EQ(s.kind, ComPortSpec::Kind::Plug) << "the loopback test plug";
    EXPECT_EQ(s.ToString(), "PLUG");
}

TEST(ComPortSpec_Test, TcpEndpoint)
{
    ComPortSpec s;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("TCP:127.0.0.1:2323", s, error)) << error;
    EXPECT_EQ(s.kind, ComPortSpec::Kind::Tcp);
    EXPECT_EQ(s.addr, 0x7F000001u);
    EXPECT_EQ(s.port, 2323);
    EXPECT_EQ(s.ToString(), "TCP:127.0.0.1:2323");
    EXPECT_FALSE(ComPortSpec::Parse("tcp:127.0.0.1", s, error));
    EXPECT_FALSE(ComPortSpec::Parse("tcp:127.0.0.1:0", s, error));
    EXPECT_FALSE(ComPortSpec::Parse("tcp:127.0.0.256:23", s, error)) << "digits and dots that are no address";
}

TEST(ComPortSpec_Test, TcpHostNames)
{
    ComPortSpec s;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("TCP:BBS.Example.org:23", s, error)) << error;
    EXPECT_EQ(s.kind, ComPortSpec::Kind::Tcp);
    EXPECT_EQ(s.host, "bbs.example.org");
    EXPECT_EQ(s.addr, 0u) << "resolved at connect time";
    EXPECT_EQ(s.ToString(), "TCP:bbs.example.org:23");
    ASSERT_TRUE(ComPortSpec::Parse("tcp:localhost:23", s, error));
    EXPECT_EQ(s.host, "localhost");
    ASSERT_TRUE(ComPortSpec::Parse("tcp:next-zxart:80", s, error));
    EXPECT_FALSE(ComPortSpec::Parse("tcp:-bad.example:23", s, error));
    EXPECT_FALSE(ComPortSpec::Parse("tcp:bad_name.example:23", s, error));
    EXPECT_FALSE(ComPortSpec::Parse("tcp:a..b:23", s, error));
}

TEST(ComPortSpec_Test, SerialDeviceAndBaud)
{
    ComPortSpec s;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("SERIAL:/dev/tty.usbserial-0001", s, error));
    EXPECT_EQ(s.kind, ComPortSpec::Kind::Serial);
    EXPECT_EQ(s.device, "/dev/tty.usbserial-0001");
    EXPECT_EQ(s.baud, 115200u);
    ASSERT_TRUE(ComPortSpec::Parse("serial:COM3,57600", s, error));
    EXPECT_EQ(s.device, "COM3");
    EXPECT_EQ(s.baud, 57600u);
    EXPECT_EQ(s.ToString(), "SERIAL:COM3,57600");
    EXPECT_FALSE(ComPortSpec::Parse("serial:", s, error));
    EXPECT_FALSE(ComPortSpec::Parse("serial:COM3,fast", s, error));
}

TEST(ComPortSpec_Test, EspModules)
{
    ComPortSpec s;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("espnet", s, error));
    EXPECT_EQ(s.kind, ComPortSpec::Kind::Espnet);
    EXPECT_EQ(s.ToString(), "ESPNET");
    EXPECT_EQ(s.baud, 0u) << "no rate: the port's default";
    ASSERT_TRUE(ComPortSpec::Parse("AT", s, error));
    EXPECT_EQ(s.kind, ComPortSpec::Kind::At);
    EXPECT_FALSE(ComPortSpec::Parse("hayes", s, error));
}

TEST(ComPortSpec_Test, EspModuleRates)
{
    ComPortSpec s;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("espnet, 38400", s, error)) << error;
    EXPECT_EQ(s.kind, ComPortSpec::Kind::Espnet);
    EXPECT_EQ(s.baud, 38400u);
    EXPECT_EQ(s.ToString(), "ESPNET,38400");
    ASSERT_TRUE(ComPortSpec::Parse("at,115200", s, error)) << error;
    EXPECT_EQ(s.kind, ComPortSpec::Kind::At);
    EXPECT_EQ(s.ToString(), "AT,115200");
    EXPECT_FALSE(ComPortSpec::Parse("ESPNET,", s, error));
    EXPECT_FALSE(ComPortSpec::Parse("AT,fast", s, error));
    EXPECT_NE(error.find("AT[,<firmware>][,<baud>]"), std::string::npos) << error;
    EXPECT_FALSE(ComPortSpec::Parse("ESPNET,0", s, error));
}

TEST(ComPortSpec_Test, AtFirmwareForOneModule)
{
    // AT[,<firmware>][,<baud>]: the build of this module alone (ZiFi=AT,ESP8266-AT222), either order
    ComPortSpec s;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("at,esp8266-at222", s, error)) << error;
    EXPECT_EQ(s.kind, ComPortSpec::Kind::At);
    EXPECT_EQ(s.firmware, 3);
    EXPECT_EQ(s.baud, 0u);
    EXPECT_EQ(s.ToString(), "AT,ESP8266-AT222");
    ASSERT_TRUE(ComPortSpec::Parse("AT,57600,2.2.1", s, error)) << error;
    EXPECT_EQ(s.firmware, 2);
    EXPECT_EQ(s.baud, 57600u);
    EXPECT_EQ(s.ToString(), "AT,ESP8266-AT221,57600");
    ASSERT_TRUE(ComPortSpec::Parse("AT", s, error));
    EXPECT_EQ(s.firmware, ComPortSpec::kDefaultFirmware) << "not written: EspChip / the board decides";
    EXPECT_FALSE(ComPortSpec::Parse("AT,ESP9000", s, error));
    EXPECT_NE(error.find("ESP8266-AT222"), std::string::npos) << error;
    EXPECT_FALSE(ComPortSpec::Parse("AT,115200,57600", s, error)) << "one rate";
    EXPECT_FALSE(ComPortSpec::Parse("ESPNET,ESP8266", s, error)) << "ESPNET has one firmware";
}

TEST(ComPortSpec_Test, ZiFiNative)
{
    ComPortSpec s;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("zifi-native", s, error)) << error;
    EXPECT_EQ(s.kind, ComPortSpec::Kind::ZiFiNative);
    EXPECT_EQ(s.ToString(), "ZIFI-NATIVE");
    ASSERT_TRUE(ComPortSpec::Parse("ZIFI-NATIVE,esp-01s", s, error)) << error;
    EXPECT_EQ(s.firmware, 1);
    EXPECT_EQ(s.ToString(), "ZIFI-NATIVE,ESP01S");
    ASSERT_TRUE(ComPortSpec::Parse("ZIFI-NATIVE,S3,115200", s, error)) << error;
    EXPECT_EQ(s.firmware, 0);
    EXPECT_EQ(s.ToString(), "ZIFI-NATIVE,S3,115200");
    EXPECT_FALSE(ComPortSpec::Parse("ZIFI-NATIVE,ESP8266-AT222", s, error)) << "an AT build is no native variant";
}
