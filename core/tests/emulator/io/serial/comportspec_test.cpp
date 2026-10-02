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
    EXPECT_FALSE(ComPortSpec::Parse("modem", s, error));
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
    EXPECT_NE(error.find("AT[,<baud>]"), std::string::npos) << error;
    EXPECT_FALSE(ComPortSpec::Parse("ESPNET,0", s, error));
}
