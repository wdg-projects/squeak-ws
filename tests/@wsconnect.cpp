#include "util.cpp"

std::string test::expected_stdout = "";
std::string test::expected_stderr = "";

int test::main()
{
    SqueakWS::WebSocket ws("ws://localhost:65500/");
    return 0;
}
