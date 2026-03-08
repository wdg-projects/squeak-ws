#include "util.cpp"

std::string test::expected_stdout = "";
std::string test::expected_stderr = "EXCEPTION: Closed socket\n";
int test::expected_runtime = 2000;

int test::main()
{
    SqueakWS::WebSocket ws("ws://localhost:65500/");
    ws.send_text("@terminate");
    ws.run();
    ws.send_text("foo");
    return 0;
}
