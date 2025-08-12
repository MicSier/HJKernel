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
    std::string filename;
    std::vector<std::string> chunks;  // Stores all valid code chunks

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

    // Writes entire session from memory chunks to file
    void write_session_file() {
        std::ofstream session_file(filename, std::ios::trunc);
        session_file << "module Main where\n\nmain::IO ()\nmain = return ()\n";
        for (const auto& chunk : chunks) {
            session_file << chunk << "\n";
        }
        session_file.close();
    }

    // Checks if reload was successful based on output
    bool is_reload_successful(const std::string& output) {
        if (output.empty()) return false;

        // Find last non-empty line
        size_t end_pos = output.find_last_not_of("\r\n");
        if (end_pos == std::string::npos) return false;

        size_t start_pos = output.substr(0, end_pos + 1).find_last_of("\r\n");
        start_pos = (start_pos == std::string::npos) ? 0 : start_pos + 1;
        std::string last_line = output.substr(start_pos, end_pos - start_pos + 1);

        // Check for success/error indicators
        if (last_line.find("Ok, ") == 0) return true;
        if (last_line.find("Failed, ") == 0) return false;

        // Fallback check for specific error message
        return (output.find("Failed, no modules loaded") == std::string::npos);
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

        char tmpPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpPath);
        filename = std::string(tmpPath) + "session.hs"; 

        chunks.clear();  // Start with empty session
        //chunks.push_back("module Main where\n\nmain::IO ()\nmain = return ()\n");
        write_session_file();

        // Load initial empty module
        std::string load_output = send(":load " + filename);
        send(":module +Main");
        wait_for_prompt();
    }

    // For commands and expressions
    std::string send(const std::string& line) {
        DWORD written;
        WriteFile(in_w, line.c_str(), (DWORD)line.size(), &written, NULL);
        WriteFile(in_w, "\n", 1, &written, NULL);
        return wait_for_prompt();
    }

    // For declarations and multi-line code
    std::string send_file_load(const std::string& code) {
        std::vector<std::string> old_chunks = chunks;  // Save state
        chunks.push_back(code);
        write_session_file();

        std::string reload_output = send(":reload");
        if (is_reload_successful(reload_output)) {
            send(":module +Main");
            return reload_output;
        }
        else {
            // Revert to previous state
            chunks = old_chunks;
            write_session_file();
            send(":reload");  // Restore working state
            return reload_output;
        }
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