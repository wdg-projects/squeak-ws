#include "util.cpp"

std::string test::expected_stdout = "";
std::string test::expected_stderr = "EXCEPTION: Could not connect to http://localhost:1 (127.0.0.1:1): Connection refused\n";

int test::main()
{
    SqueakWS::WebSocket ws("ws://localhost:1/");
    return 0;
}
