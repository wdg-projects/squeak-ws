#include "util.cpp"

std::string test::expected_stdout = "";
std::string test::expected_stderr = "";
int test::expected_runtime = 1000;

int test::main()
{
    SqueakWS::WebSocket ws("ws://localhost:65500/");
    ws.connect();
    return 0;
}
