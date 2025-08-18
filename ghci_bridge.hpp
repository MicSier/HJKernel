#ifndef GHCIBRIDGE_HPP
#define GHCIBRIDGE_HPP

#include <windows.h>
#include <string>
#include <regex>
#include <vector>
#include <fstream>

struct GHCiBridge {
    HANDLE in_w = NULL, out_r = NULL;
    PROCESS_INFORMATION pi = {};

    std::string wait_for_prompt() {
        std::string acc;
        DWORD read;
        CHAR buf[1024];
        const std::string prompt = "ghci>";

        while (true) {
            if (!ReadFile(out_r, buf, sizeof(buf) - 1, &read, NULL) || read == 0) break;
            buf[read] = '\0';
            acc += buf;
            size_t pos = acc.find(prompt);
            if (pos != std::string::npos) {
                acc.erase(pos);
                break;
            }

        }
        static const std::regex ansi_pattern(R"(\x1B\[[0-9;]*[A-Za-z])");
        return std::regex_replace(acc, ansi_pattern, "");
    }

    void start() {
        SECURITY_ATTRIBUTES saAttr{ sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
        HANDLE in_r, out_w;
        CreatePipe(&out_r, &out_w, &saAttr, 0);
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
        CreatePipe(&in_r, &in_w, &saAttr, 0);
        SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.hStdError = out_w;
        si.hStdOutput = out_w;
        si.hStdInput = in_r;
        si.dwFlags |= STARTF_USESTDHANDLES;
        wchar_t cmd[] = L"ghci.exe";
        CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
        CloseHandle(out_w);
        CloseHandle(in_r);

        wait_for_prompt();
    }

    std::string send(const std::string& line) {
        DWORD written;
        WriteFile(in_w, ":{", 2, &written, NULL);
        WriteFile(in_w, "\n", 1, &written, NULL);
        WriteFile(in_w, line.c_str(), (DWORD)line.size(), &written, NULL);
        WriteFile(in_w, "\n", 1, &written, NULL);
        WriteFile(in_w, ":}", 2, &written, NULL);
        WriteFile(in_w, "\n", 1, &written, NULL);

        std::string res = wait_for_prompt();

        const std::string multiline_prompt = "ghci| ";
        size_t pos = 0;
        while ((pos = res.find(multiline_prompt, pos)) != std::string::npos) {
            res.erase(pos, multiline_prompt.length());
        }

        return res;
    }

    void stop() {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(in_w);
        CloseHandle(out_r);
    }
};

#endif