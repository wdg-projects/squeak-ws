#include "util.cpp"

std::string test::expected_stdout = "Closed\nClosed\nClosed\nClosed\n";
std::string test::expected_stderr = "";
int test::expected_runtime = 2000;

using namespace SqueakWS;

#define EXPECT(...) try { __VA_ARGS__ } catch (ClosedSocketError &) { std::cout << "Closed" << std::endl; }

int test::main()
{
    WebSocket ws("ws://localhost:65500/");
    ws.close(1000);

    EXPECT(ws.send_text("foo");)
    EXPECT(std::string s = "bar"; ws.send_binary(s.begin(), s.end());)
    EXPECT(ws.close(1234);)
    EXPECT(ws.run();)

    ws.close_now();

    return 0;
}
