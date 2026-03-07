#include <sys/poll.h>
#define RUNNER
#include "util.cpp"

#include <vector>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>

#include <filesystem>

int spawn_bg(std::string path, std::vector<std::string> args, int *pstdout, int *pstderr)
{
    std::vector<const char *> argv = { path.c_str() };
    for (const auto &arg : args)
        argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    int pipefds_stdout[2], pipefds_stderr[2];
    if (pstdout || pstderr) {
        pipe(pipefds_stdout);
        pipe(pipefds_stderr);
    }

    int c = fork();
    if (c == 0) {
        if (pstdout)
            dup2(pipefds_stdout[1], 1);
        if (pstderr)
            dup2(pipefds_stderr[1], 2);
        execvpe(path.c_str(), (char *const *)&argv[0], nullptr);
        perror("Couldn't start");
        _exit(255);
    }
    if (pstdout)
        *pstdout = pipefds_stdout[0];
    if (pstderr)
        *pstderr = pipefds_stderr[0];
    return c;
}

std::tuple<int, std::string, std::string> spawn_and_collect(std::string path, std::vector<std::string> args)
{
    int fdstdout, fdstderr;
    int c = spawn_bg(path, args, &fdstdout, &fdstderr);

    int stat;
    std::string pstdout, pstderr;
    while (true) {
        struct pollfd fds[] = {
            { fdstdout, POLLIN, 0 },
            { fdstderr, POLLIN, 0 }
        };
        if (poll(fds, 2, 10) < 0) {
            perror("poll");
            abort();
        }
        bool x = false;
        static char buf[1024];
        if (fds[0].revents & POLLIN) {
            int nread;
            if ((nread = read(fds[0].fd, buf, 1024)) < 0) {
                perror("read");
                abort();
            }
            pstdout.append(std::string{ buf, (size_t)nread });
            x = true;
        }
        if (fds[1].revents & POLLIN) {
            int nread;
            if ((nread = read(fds[1].fd, buf, 1024)) < 0) {
                perror("read");
                abort();
            }
            pstderr.append(std::string{ buf, (size_t)nread });
            x = true;
        }
        if (!x) {
            int res = waitpid(c, &stat, WNOHANG);
            if (res < 0) {
                perror("waitpid");
                abort();
            }
            if (res != 0)
                break;
        }
    }

    close(fdstdout);
    close(fdstderr);

    return { WEXITSTATUS(stat), pstdout, pstderr };
}

int main(int argc, char **argv)
{
    int c = spawn_bg("node", { "sampleserver.js" }, nullptr, nullptr);

    int mode = 0;
    parseargs({ }, 0, 0, argc, argv);

    int total = 0, success = 0;
    for (const auto &entry : std::filesystem::directory_iterator{std::filesystem::path(".")}) {
        std::string fn = entry.path().filename().string();
        if (fn.starts_with("@") && !fn.ends_with(".cpp")) {
            ++total;

            std::cout << fn << ": ";

            auto expected_stdout = std::get<1>(spawn_and_collect(entry.path().string(), { "-O" }));
            auto expected_stderr = std::get<1>(spawn_and_collect(entry.path().string(), { "-E" }));

            auto [ rc, pstdout, pstderr ] = spawn_and_collect(entry.path().string(), { });
            if (rc != 0) {
                std::cerr << pstderr << std::endl;
                std::cout << std::format("FAIL (error code {})", rc) << std::endl;
            } else if (pstdout != expected_stdout) {
                std::cerr << pstdout << std::endl;
                std::cout << "FAIL (stdout)" << std::endl;
            } else if (pstderr != expected_stderr) {
                std::cerr << pstderr << std::endl;
                std::cout << "FAIL (stderr)" << std::endl;
            } else {
                std::cout << "OK" << std::endl;
                ++success;
            }
        }
    }
    std::cout << std::format("DONE {}/{}", success, total) << std::endl;
    kill(c, SIGTERM);
    return success != total;
}
