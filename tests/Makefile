TESTS = @terminate @wsconnect @wsconnect-invalid

CPP = g++
CPPFLAGS = -g -std=c++20 -Wall -Wextra -pedantic
LDFLAGS = -lssl -lcrypto

test-quiet: build-tools
		./runner 2>/dev/null

test: build-tools
		./runner

clean:
		-rm $(TESTS) runner

build-tools: $(TESTS) runner

runner: runner.cpp util.cpp
		$(CPP) -std=c++20 -Wall -Wextra -pedantic -o runner runner.cpp

@%: @%.cpp util.cpp ../squeakws.hpp
		$(CPP) $(CPPFLAGS) -o $@ $< $(LDFLAGS)

.PHONY: test-quiet build-tools clean
