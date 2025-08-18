#ifndef HISTORY_HPP
#define HISTORY_HPP

#include <vector>
#include <string>
#include <set>
#include <regex>

#include"jupyter_protocol.hpp"

struct HistoryEntry {
    int session;
    int line_number;
    std::string input;
    std::string output;
};

std::vector<HistoryEntry> execution_history;

std::vector<HistoryEntry> get_history_range(int session, int start, int stop) {
    std::vector<HistoryEntry> result;
    for (const auto& entry : execution_history) {
        if (entry.session == session &&
            entry.line_number >= start &&
            entry.line_number < stop) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<HistoryEntry> get_history_tail(int n) {
    std::vector<HistoryEntry> result;
    if (n <= 0) return result;

    int total = static_cast<int>(execution_history.size());
    int start = max(0, total - n);
    for (int i = start; i < total; ++i) {
        result.push_back(execution_history[i]);
    }
    return result;
}

std::vector<HistoryEntry> search_history(const std::string& pattern, bool unique) {
    std::vector<HistoryEntry> result;
    std::set<std::string> seen;

    std::string regex_pattern = std::regex_replace(pattern, std::regex("\\*"), ".*");
    regex_pattern = std::regex_replace(regex_pattern, std::regex("\\?"), ".");
    std::regex re(regex_pattern, std::regex::icase);

    for (const auto& entry : execution_history) {
        if (std::regex_match(entry.input, re)) {
            if (unique) {
                if (seen.find(entry.input) != seen.end()) {
                    continue;
                }
                seen.insert(entry.input);
            }
            result.push_back(entry);
        }
    }
    return result;
}

void send_history_reply(const std::vector<HistoryEntry>& entries,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    bool include_output,
    const std::string& key,
    zmq::socket_t& socket)
{
    JsonValue content;
    content.type = JsonValue::Object;
    content.o["status"] = JsonValue{ JsonValue::String, false, 0.0, "ok" };

    JsonValue history;
    history.type = JsonValue::Array;

    for (auto& e : entries) {
        JsonValue tuple;
        tuple.type = JsonValue::Array;
        tuple.a.push_back(JsonValue{ JsonValue::Number, false, (double)e.session });
        tuple.a.push_back(JsonValue{ JsonValue::Number, false, (double)e.line_number });

        if (include_output) {
            JsonValue inout;
            inout.type = JsonValue::Array;
            inout.a.push_back(JsonValue{ JsonValue::String, false, 0.0, e.input });
            inout.a.push_back(JsonValue{ JsonValue::String, false, 0.0, e.output });
            tuple.a.push_back(inout);
        }
        else {
            tuple.a.push_back(JsonValue{ JsonValue::String, false, 0.0, e.input });
        }

        history.a.push_back(tuple);
    }

    content.o["history"] = history;

    send_message("history_reply", content, parent_header, identities, key, socket);
}

void handle_history_request(const JsonValue& content,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket)
{
    bool output = content.o.at("output").b;
    bool raw = content.o.at("raw").b;
    std::string hist_type = content.o.at("hist_access_type").s;

    int session = content.o.count("session") ? (int)content.o.at("session").n : 0;
    int start = content.o.count("start") ? (int)content.o.at("start").n : 0;
    int stop = content.o.count("stop") ? (int)content.o.at("stop").n : 0;
    int n = content.o.count("n") ? (int)content.o.at("n").n : 10;
    std::string pattern = content.o.count("pattern") ? content.o.at("pattern").s : "";
    bool unique = content.o.count("unique") ? content.o.at("unique").b : false;

    std::vector<HistoryEntry> selected;

    if (hist_type == "range") {
        selected = get_history_range(session, start, stop);
    }
    else if (hist_type == "tail") {
        selected = get_history_tail(n);
    }
    else if (hist_type == "search") {
        selected = search_history(pattern, unique);
    }
    else {
        std::cerr << "Unhandled history type " << hist_type << std::endl;
        exit(1);
    }

    send_history_reply(selected, parent_header, identities, output, key, socket);
}

#endif // HISTORY_HPP