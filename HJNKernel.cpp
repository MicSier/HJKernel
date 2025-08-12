// main.cpp

#define _WINSOCKAPI_
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <intrin.h>
#include <zmq.hpp>
#include "json_parser.hpp"
#include "ghci_bridge.hpp"
#include "sha256.hpp"
#include <thread>
#include <random>
#include <chrono>
#include <thread>
#include <iomanip>
#include <unordered_map>
#include <functional>
#include <set>
#include <regex>

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

std::string make_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
        << std::setw(8) << dist(gen) << "-"
        << std::setw(4) << (dist(gen) & 0xFFFF) << "-"
        << std::setw(4) << ((dist(gen) & 0x0FFF) | 0x4000) << "-"
        << std::setw(4) << ((dist(gen) & 0x3FFF) | 0x8000) << "-"
        << std::setw(8) << dist(gen)
        << std::setw(4) << (dist(gen) & 0xFFFF);
    return ss.str();
}

std::string make_jupyter_style_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    static int counter = 0;

    // Generate standard UUID part
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
        << std::setw(8) << dist(gen) << "-"
        << std::setw(4) << (dist(gen) & 0xFFFF) << "-"
        << std::setw(4) << ((dist(gen) & 0x0FFF) | 0x4000) << "-"
        << std::setw(4) << ((dist(gen) & 0x3FFF) | 0x8000) << "-"
        << std::setw(8) << dist(gen)
        << std::setw(4) << (dist(gen) & 0xFFFF);

    // Add process ID and counter
    ss << "_" << GetCurrentProcessId() << "_" << ++counter;
    return ss.str();
}

std::string iso8601_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    // Get microseconds
    auto duration = now.time_since_epoch();
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration) % 1000000;

    struct tm utc_tm;
    if (gmtime_s(&utc_tm, &t) != 0) {
        return "";  // return empty string if conversion fails
    }

    std::ostringstream ss;
    ss << std::put_time(&utc_tm, "%FT%T");
    ss << "." << std::setfill('0') << std::setw(6) << microseconds.count() << "Z";
    return ss.str();
}

void send_status(zmq::socket_t& iopub_sock, const std::string& execution_state, const std::string& session, const std::string& key) {
    // Create header
    JsonValue header;
    header.type = JsonValue::Object;
    header.o["msg_id"] = JsonValue{ JsonValue::String, false, 0.0, make_uuid() };
    header.o["msg_type"] = JsonValue{ JsonValue::String, false, 0.0, "status" };
    header.o["username"] = JsonValue{ JsonValue::String, false, 0.0, "username" };
    header.o["session"] = JsonValue{ JsonValue::String, false, 0.0, session };
    header.o["date"] = JsonValue{ JsonValue::String, false, 0.0, iso8601_now() };
    header.o["version"] = JsonValue{ JsonValue::String, false, 0.0, "5.3" };

    // Create content
    JsonValue content;
    content.type = JsonValue::Object;
    content.o["execution_state"] = JsonValue{ JsonValue::String, false, 0.0, execution_state };

    JsonValue metadata;
    metadata.type = JsonValue::Object;

    JsonValue parent_header;
    parent_header.type = JsonValue::Object;

    std::string header_str = header.to_string();
    std::string parent_str = parent_header.to_string();
    std::string meta_str = metadata.to_string();
    std::string content_str = content.to_string();

    // Calculate HMAC signature
    std::string signature = hmac_sha256(key, header_str + parent_str + meta_str + content_str);

    // IOPub uses topic as first frame
    std::string topic = "status";
    iopub_sock.send(zmq::message_t(topic.begin(), topic.end()), zmq::send_flags::sndmore);

    // Send delimiter
    const std::string delimiter = "<IDS|MSG>";
    iopub_sock.send(zmq::message_t(delimiter.data(), delimiter.size()), zmq::send_flags::sndmore);

    // Send HMAC signature
    iopub_sock.send(zmq::message_t(signature.begin(), signature.end()), zmq::send_flags::sndmore);

    // Send message parts
    iopub_sock.send(zmq::message_t(header_str.begin(), header_str.end()), zmq::send_flags::sndmore);
    iopub_sock.send(zmq::message_t(parent_str.begin(), parent_str.end()), zmq::send_flags::sndmore);
    iopub_sock.send(zmq::message_t(meta_str.begin(), meta_str.end()), zmq::send_flags::sndmore);
    iopub_sock.send(zmq::message_t(content_str.begin(), content_str.end()), zmq::send_flags::none);
}



void send_kernel_info_reply(zmq::socket_t& sock,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    const std::string& session,
    const std::string& parent_header)
{
    //std::string parent_msg_id = parent_header.o["msg_id"].s;
    //std::string parent_msg_type = parent_header.o["msg_type"].s;

    // Create header
    JsonValue header;
    header.type = JsonValue::Object;
    header.o["msg_id"] = JsonValue{ JsonValue::String, false, 0.0, make_jupyter_style_id()}; //"kernel-info-" + parent_msg_id 
    header.o["msg_type"] = JsonValue{ JsonValue::String, false, 0.0, "kernel_info_reply" };
    header.o["username"] = JsonValue{ JsonValue::String, false, 0.0, "username" };
    header.o["session"] = JsonValue{ JsonValue::String, false, 0.0, session };
    header.o["date"] = JsonValue{ JsonValue::String, false, 0.0, iso8601_now() };
    header.o["version"] = JsonValue{ JsonValue::String, false, 0.0, "5.3" };

    // Create content
    JsonValue content;
    content.type = JsonValue::Object;
    content.o["status"] = JsonValue{ JsonValue::String, false, 0.0, "ok" };
    content.o["protocol_version"] = JsonValue{ JsonValue::String, false, 0.0, "5.3" };
    content.o["implementation"] = JsonValue{ JsonValue::String, false, 0.0, "haskell-cpp" };
    content.o["implementation_version"] = JsonValue{ JsonValue::String, false, 0.0, "0.1" };
    content.o["banner"] = JsonValue{ JsonValue::String, false, 0.0, "Simple kernel for haskell suport in Jupyter" };

    JsonValue language_info;
    language_info.type = JsonValue::Object;
    language_info.o["name"] = JsonValue{ JsonValue::String, false, 0.0, "haskell" };
    language_info.o["version"] = JsonValue{ JsonValue::String, false, 0.0, "9.8" };
    language_info.o["mimetype"] = JsonValue{ JsonValue::String, false, 0.0, "text/x-haskell" };
    language_info.o["file_extension"] = JsonValue{ JsonValue::String, false, 0.0, ".hs" };
    content.o["language_info"] = language_info;

    // Create help_links array
    JsonValue help_links;
    help_links.type = JsonValue::Array;

    // Create a help link object
    JsonValue help_link;
    help_link.type = JsonValue::Object;
    help_link.o["text"] = JsonValue{ JsonValue::String, false, 0.0, "Help" };
    help_link.o["url"] = JsonValue{ JsonValue::String, false, 0.0, "https://example.com" };

    // Add the help link to the array
    help_links.a.push_back(help_link);

    // Add to content
    content.o["help_links"] = help_links;

    // Add debugger field
    content.o["debugger"] = JsonValue{ JsonValue::Bool, false, 0.0, "" };
    //content.o["banner"] = JsonValue{ JsonValue::String, false, 0.0, "Haskell kernel" };

    JsonValue metadata;
    metadata.type = JsonValue::Object;

    std::string header_str = header.to_string();
    //std::string parent_str = parent_header;
    std::string meta_str = metadata.to_string();
    std::string content_str = content.to_string();
    

    // Send identities
    for (const auto& id : identities) {
        zmq::message_t msg(id.size());
        memcpy(msg.data(), id.data(), id.size());
        //std::cout << "Sending ident " << msg.to_string()<<"\n";
        sock.send(std::move(msg), zmq::send_flags::sndmore);
    }

    // Send delimiter
    //sock.send(zmq::message_t("<IDS|MSG>", 8), zmq::send_flags::sndmore);
    const std::string delimiter = "<IDS|MSG>";
    sock.send(zmq::message_t(delimiter.data(), delimiter.size()), zmq::send_flags::sndmore);
    ////std::cout << "Sending delimiter " << "<IDS|MSG>" << std::endl;
    // Send signature (skip signing for now)
    std::string signature = hmac_sha256(key, header_str + parent_header + meta_str + content_str);
    sock.send(zmq::message_t(signature.begin(), signature.end()), zmq::send_flags::sndmore);
    //std::cout << "Lock " << header_str + parent_header + meta_str + content_str << std::endl;
    //std::cout << "Key " << key << std::endl;
    //std::cout << "Signature " << signature << std::endl;
    // Send header, parent_header, metadata, content
    sock.send(zmq::message_t(header_str.begin(), header_str.end()), zmq::send_flags::sndmore);
    //std::cout << "Sending header " << header_str << std::endl;
    sock.send(zmq::message_t(parent_header.begin(), parent_header.end()), zmq::send_flags::sndmore);
    //std::cout << "Sending parent " << parent_header << std::endl;
    sock.send(zmq::message_t(meta_str.begin(), meta_str.end()), zmq::send_flags::sndmore);
    //std::cout << "Sending metadata " << meta_str << std::endl;
    sock.send(zmq::message_t(content_str.begin(), content_str.end()), zmq::send_flags::none);
    //std::cout << "Sending content " << content_str << std::endl;
}

void send_execute_result(zmq::socket_t& sock,
    const std::vector<zmq::message_t>& identities,
    const JsonValue& parent_header,
    const std::string& result,
    int execution_count,
    const std::string& key)
{
    // Escape quotes and backslashes in the result
    std::string escaped_result;
    for (char c : result) {
        if (c == '\\') escaped_result += "\\\\";
        else if (c == '"') escaped_result += "\\\"";
        else escaped_result += c;
    }

    // Build header
    JsonValue header(JsonValue::Object);
    header.o["msg_id"] = JsonValue{ JsonValue::String, false, 0.0, make_uuid() };
    header.o["msg_type"] = JsonValue{ JsonValue::String, false, 0.0, "execute_result" };
    header.o["username"] = JsonValue{ JsonValue::String, false, 0.0, "kernel" };
    header.o["session"] = parent_header.o.at("session");
    header.o["date"] = JsonValue{ JsonValue::String, false, 0.0, iso8601_now() };
    header.o["version"] = JsonValue{ JsonValue::String, false, 0.0,"5.3" };

    // Build content
    JsonValue content(JsonValue::Object);
    content.o["execution_count"] = JsonValue{ JsonValue::Number, false, (double)execution_count, "" };

    JsonValue data(JsonValue::Object);
    data.o["text/plain"] = JsonValue{ JsonValue::String, false, 0.0, result };

    content.o["data"] = data;
    content.o["metadata"] = JsonValue(JsonValue::Object);

    // Empty metadata for top-level
    JsonValue metadata(JsonValue::Object);

    // Convert JSON objects to strings
    std::string header_json = header.to_string();
    std::string parent_header_json = parent_header.to_string();
    std::string metadata_json = metadata.to_string();
    std::string content_json = content.to_string();

    // Compute HMAC signature
    std::string signature = hmac_sha256(key, header_json+ parent_header_json+ metadata_json+ content_json);

    // Send multipart message
    for (const auto& id : identities) {
        zmq::message_t id_copy(id.size());
        memcpy(id_copy.data(), id.data(), id.size());
        sock.send(std::move(id_copy), zmq::send_flags::sndmore);
    }

    const std::string delimiter = "<IDS|MSG>";
    sock.send(zmq::message_t(delimiter.data(), delimiter.size()), zmq::send_flags::sndmore);
    sock.send(zmq::message_t(signature.data(), signature.size()), zmq::send_flags::sndmore);
    sock.send(zmq::message_t(header_json.data(), header_json.size()), zmq::send_flags::sndmore);
    sock.send(zmq::message_t(parent_header_json.data(), parent_header_json.size()), zmq::send_flags::sndmore);
    sock.send(zmq::message_t(metadata_json.data(), metadata_json.size()), zmq::send_flags::sndmore);
    sock.send(zmq::message_t(content_json.data(), content_json.size()), zmq::send_flags::none);
}


struct HistoryEntry {
    int session;
    int line_number;
    std::string input;
    std::string output; // empty if no output
};

std::vector<HistoryEntry> execution_history;

// Get history for a specific session and range of lines
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

// Get the last `n` history entries (any session)
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

// Search history for entries matching a glob-like pattern
std::vector<HistoryEntry> search_history(const std::string& pattern, bool unique) {
    std::vector<HistoryEntry> result;
    std::set<std::string> seen;

    // Convert glob (* ?) to regex
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


void send_message(const std::string& msg_type,
    const JsonValue& content,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket
)
{
    JsonValue header;
    header.type = JsonValue::Object;
    header.o["msg_id"] = JsonValue{ JsonValue::String, false, 0.0, make_uuid() };
    header.o["username"] = JsonValue{ JsonValue::String, false, 0.0, "user" };
    header.o["session"] = parent_header.o.at("session");
    header.o["date"] = JsonValue{ JsonValue::String, false, 0.0, iso8601_now() };
    header.o["msg_type"] = JsonValue{ JsonValue::String, false, 0.0, msg_type };
    header.o["version"] = JsonValue{ JsonValue::String, false, 0.0, "5.3" };

    JsonValue metadata;
    metadata.type = JsonValue::Object;

    // Serialize and sign
    std::string header_json = header.to_string();
    std::string parent_json = parent_header.to_string();
    std::string meta_json = metadata.to_string();
    std::string content_json = content.to_string();

    std::string sig = hmac_sha256(key,
        header_json + parent_json + meta_json + content_json);

    // Send frames: [identities, "<IDS|MSG>", sig, header, parent, metadata, content]

    // Send identities
    for (const auto& id : identities) {
        zmq::message_t msg(id.size());
        memcpy(msg.data(), id.data(), id.size());
        //std::cout << "Sending ident " << msg.to_string() << "\n";
        socket.send(std::move(msg), zmq::send_flags::sndmore);
    }

    //socket.send(zmq::buffer("<IDS|MSG>"), zmq::send_flags::sndmore);
    const std::string delimiter = "<IDS|MSG>";
    socket.send(zmq::message_t(delimiter.data(), delimiter.size()), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(sig), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(header_json), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(parent_json), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(meta_json), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(content_json), zmq::send_flags::none);
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
    bool output = content.o.at("output").b; // bool
    bool raw = content.o.at("raw").b;
    std::string hist_type = content.o.at("hist_access_type").s;

    // Optional fields — check presence before using
    int session = content.o.count("session") ? (int)content.o.at("session").n : 0;
    int start = content.o.count("start") ? (int)content.o.at("start").n : 0;
    int stop = content.o.count("stop") ? (int)content.o.at("stop").n : 0;
    int n = content.o.count("n") ? (int)content.o.at("n").n : 10;
    std::string pattern = content.o.count("pattern") ? content.o.at("pattern").s : "";
    bool unique = content.o.count("unique") ? content.o.at("unique").b : false;

    // Now select the right slice of your stored history:
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
        // Invalid hist_access_type — could send error
    }

    send_history_reply(selected, parent_header, identities, output, key, socket);
}

struct CommInstance {
    std::string target_name;
    JsonValue init_data;
};

std::unordered_map<std::string, std::function<void(const std::string&, const JsonValue&)>> comm_targets;
std::unordered_map<std::string, CommInstance> active_comms;

bool comm_target_exists(const std::string& name) {
    return comm_targets.find(name) != comm_targets.end();
}

bool comm_instance_exists(const std::string& id) {
    return active_comms.find(id) != active_comms.end();
}

void create_comm_instance(const std::string& comm_id,
    const std::string& target_name,
    const JsonValue& init_data)
{
    CommInstance inst;
    inst.target_name = target_name;
    inst.init_data = init_data;

    active_comms[comm_id] = inst;

    // Call registered target handler if it exists
    auto it = comm_targets.find(target_name);
    if (it != comm_targets.end()) {
        it->second(comm_id, init_data);
    }
}

/*void handle_comm_data(const std::string& comm_id,
    const JsonValue& data)
{
    auto it = active_comms.find(comm_id);
    if (it == active_comms.end()) {
        std::cerr << "[COMM] Received data for unknown comm_id=" << comm_id << "\n";
        return;
    }

    // For now, just log it — real widgets would interpret it
    std::cerr << "[COMM] Data for comm_id=" << comm_id
        << " = " << data.to_string() << "\n";
}*/

void handle_comm_data(const std::string& comm_id,
    const JsonValue& data,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket)
{
    auto it = active_comms.find(comm_id);
    if (it == active_comms.end()) {
        std::cerr << "[COMM] Received data for unknown comm_id=" << comm_id << "\n";
        return;
    }

    // Log the received data
    std::cerr << "[COMM] Data for comm_id=" << comm_id
        << " = " << data.to_string() << "\n";

    // Build an echo payload
    JsonValue content;
    content.type = JsonValue::Object;
    content.o["comm_id"] = JsonValue{ JsonValue::String, false, 0.0, comm_id };
    content.o["data"] = data; // send back exactly what we got

    // Send comm_msg back to the frontend
    send_message(comm_id, data, parent_header, identities, key, socket);
}


void destroy_comm_instance(const std::string& comm_id)
{
    auto it = active_comms.find(comm_id);
    if (it != active_comms.end()) {
        std::cerr << "[COMM] Destroying comm_id=" << comm_id
            << " target=" << it->second.target_name << "\n";
        active_comms.erase(it);
    }
}

void send_comopen_reply(const std::vector<HistoryEntry>& entries,
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

void handle_comm_open(const JsonValue& content,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket)
{
    std::string comm_id = content.o.at("comm_id").s;
    std::string target_name = content.o.at("target_name").s;
    JsonValue data = content.o.at("data");

    // Look up registered comm targets
    if (!comm_target_exists(target_name))
    {
        // Unknown target — send comm_close immediately
        JsonValue close_content;
        close_content.type = JsonValue::Object;
        close_content.o["comm_id"] = JsonValue{ JsonValue::String, false, 0.0, comm_id };
        close_content.o["data"] = JsonValue{ JsonValue::Object }; // empty dict

        send_message("comm_close", close_content, parent_header, identities, key, socket);
        return;
    }

    // Create local Comm instance
    create_comm_instance(comm_id, target_name, data);

    // No reply needed unless your comm implementation wants to
}

void handle_comm_msg(const JsonValue& content,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket)
{
    std::string comm_id = content.o.at("comm_id").s;
    JsonValue data = content.o.at("data");

    if (!comm_instance_exists(comm_id)) {
        // Unknown comm_id — send comm_close
        JsonValue close_content;
        close_content.type = JsonValue::Object;
        close_content.o["comm_id"] = JsonValue{ JsonValue::String, false, 0.0, comm_id };
        close_content.o["data"] = JsonValue{ JsonValue::Object };
        send_message("comm_close", close_content, parent_header, identities, key, socket);
        return;
    }

    // Pass data to comm instance for handling
    handle_comm_data(comm_id, data, parent_header, identities, key, socket);
}

void handle_comm_close(const JsonValue& content,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities)
{
    std::string comm_id = content.o.at("comm_id").s;

    // Remove comm instance from map
    destroy_comm_instance(comm_id);
}

void send_comm_open(const std::string& comm_id,
    const std::string& target_name,
    const JsonValue& data,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket)
{
    JsonValue content;
    content.type = JsonValue::Object;
    content.o["comm_id"] = JsonValue{ JsonValue::String, false, 0.0, comm_id };
    content.o["target_name"] = JsonValue{ JsonValue::String, false, 0.0, target_name };
    content.o["data"] = data;

    send_message("comm_open", content, parent_header, identities, key, socket);
}

void send_comm_msg(const std::string& comm_id,
    const JsonValue& data,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket)
{
    JsonValue content;
    content.type = JsonValue::Object;
    content.o["comm_id"] = JsonValue{ JsonValue::String, false, 0.0, comm_id };
    content.o["data"] = data;

    send_message("comm_msg", content, parent_header, identities, key, socket);
}

void send_comm_close(const std::string& comm_id,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket)
{
    JsonValue content;
    content.type = JsonValue::Object;
    content.o["comm_id"] = JsonValue{ JsonValue::String, false, 0.0, comm_id };
    content.o["data"] = JsonValue{ JsonValue::Object };

    send_message("comm_close", content, parent_header, identities, key, socket);
}

void handle_comm_info_request(const JsonValue& content,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket)
{
    // Optional field: target_name
    std::string target_name;
    if (content.o.count("target_name")) {
        target_name = content.o.at("target_name").s;
    }

    // Build the 'comms' dictionary
    JsonValue comms(JsonValue::Object);

    // Assuming you have a global or member structure storing active comms:
    // std::map<std::string, std::string> active_comms; // comm_id -> target_name
    for (const auto& kv : active_comms) {
        const std::string& comm_id = kv.first;
        //const std::string& tname = kv.second;
        const CommInstance inst = kv.second;

        if (target_name.empty() || inst.target_name == target_name) {
            JsonValue comm_info(JsonValue::Object);
            comm_info.o["target_name"] = JsonValue{ JsonValue::String, false, 0.0, inst.target_name };
            comms.o[comm_id] = comm_info;
        }
    }

    // Build reply content
    JsonValue reply_content(JsonValue::Object);
    reply_content.o["status"] = JsonValue{ JsonValue::String, false, 0.0, "ok"};
    reply_content.o["comms"] = comms;

    // Send reply
    send_message("comm_info_reply", reply_content, parent_header, identities, key, socket);
}

void send_execute_input( const std::string& code,
    int execution_count,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket) {

    JsonValue content;
    content.type = JsonValue::Object;
    content.o["code"] = JsonValue{ JsonValue::String, false, 0.0, code };
    content.o["execution_count"] = JsonValue{ JsonValue::Number, false, (double)execution_count, "" };

    send_message("execute_input", content, parent_header, identities, key, socket);
}

void send_execute_reply( int execution_count,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket) {

    JsonValue content;
    content.type = JsonValue::Object;
    content.o["status"] = JsonValue{ JsonValue::String, false, 0.0, "ok"};
    content.o["execution_count"] = JsonValue{ JsonValue::Number, false, (double)execution_count, "" };
    JsonValue expressions = JsonValue{ JsonValue::Object };
    content.o["user_expressions"] = expressions;
    content.o["payload"] = JsonValue{ JsonValue::Array, {} };

    send_message("execute_reply", content, parent_header, identities, key, socket);
}


size_t exec_counter;

int main(int argc, char* argv[]) {
    exec_counter = 0;
    //Sleep(5000);
    //__debugbreak();
    //std::cerr << "Waiting for Jupyter to connect...\n";
    //std::this_thread::sleep_for(std::chrono::seconds(1));

    if (argc < 2) {
        std::cerr << "Usage: haskell_kernel.exe connection.json\n";
        return 1;
    }
    //std::cout <<"connection.json " << argv[1] << std::endl;
    std::string conn_json = read_file(argv[1]);
    if (conn_json.empty()) {
        std::cerr << "Could not read connection file.\n";
        return 1;
    }

    Parser parser(conn_json);
    JsonValue conn = parser.parse_value();

    std::string transport = conn.o["transport"].s;
    std::string ip = conn.o["ip"].s;
    std::string key = conn.o["key"].s;
    int shell_port = (int)conn.o["shell_port"].n;
    int iopub_port = (int)conn.o["iopub_port"].n;
    int stdin_port = (int)conn.o["stdin_port"].n;
    int control_port = (int)conn.o["control_port"].n;
    int hb_port = (int)conn.o["hb_port"].n;

    std::string shell_addr = transport + "://" + ip + ":" + std::to_string(shell_port);
    std::string iopub_addr = transport + "://" + ip + ":" + std::to_string(iopub_port);
    std::string stdin_addr = transport + "://" + ip + ":" + std::to_string(stdin_port);
    std::string control_addr = transport + "://" + ip + ":" + std::to_string(control_port);
    std::string hb_addr = transport + "://" + ip + ":" + std::to_string(hb_port);

    //std::cout << shell_addr << std::endl;
    zmq::context_t ctx(1);
    ctx.set(zmq::ctxopt::socket_limit, 5);
    zmq::socket_t shell(ctx, zmq::socket_type::router);
    shell.bind(shell_addr);
    zmq::socket_t iopub(ctx, zmq::socket_type::pub);
    iopub.bind(iopub_addr);
    zmq::socket_t stdin_(ctx, zmq::socket_type::router);
    stdin_.bind(stdin_addr);
    zmq::socket_t control(ctx, zmq::socket_type::router);
    control.bind(control_addr);
    zmq::socket_t hb(ctx, zmq::socket_type::rep);
    hb.bind(hb_addr);

    //shell.set(zmq::sockopt::routing_id, std::string("kernel-shell"));
    //shell.set(zmq::sockopt::router_mandatory, true);
    //shell.set(zmq::sockopt::rcvtimeo, 1000);

    GHCiBridge ghci;
    ghci.start();

    while (true) {
        std::vector<zmq::message_t> parts;
        bool got_message = false;

        // Poll with 1 second timeout
        zmq::pollitem_t items[] = {
            { static_cast<void*>(shell), 0, ZMQ_POLLIN, 0 }
        };
        //std::cout << "Polling for shell message..." << std::endl;
        int rc = zmq::poll(items, 1, std::chrono::milliseconds(1000));
        //std::cout << "Poll complete, revents: " << items[0].revents << std::endl;
        if (rc == -1) {
            // Interrupted or error
            continue;
        }

        if (!(items[0].revents & ZMQ_POLLIN)) {
            //std::cout << "No message yet." << std::endl;
            continue;
        }

        // Receive all message parts
        while (true) {
            zmq::message_t part;
            zmq::recv_result_t received = shell.recv(part, zmq::recv_flags::none);
            if (!received.has_value()) {
                std::cerr << "Receive failed." << std::endl;
                break;
            }
            parts.push_back(std::move(part));
            got_message = true;
            bool more = shell.get(zmq::sockopt::rcvmore);
            if (!more) break;
        }

        

        //std::cout << "Message has " << parts.size() << " parts:" << std::endl;
       /* for (size_t j = 0; j < parts.size(); ++j) {
            std::string part_str(static_cast<char*>(parts[j].data()), parts[j].size());
            std::cout << "Part " << j << " (size=" << parts[j].size() << "): '" << part_str << "'" << std::endl;
        }*/

        if (!got_message) continue;
        if (parts.size() < 6) {
            std::cerr << "Incomplete message received: parts=" << parts.size() << std::endl;
            continue;
        }

        // Process message...
        // Extract identities (everything before "<IDS|MSG>")
        size_t i = 0;
        std::vector<zmq::message_t> identities;
        for (; i < parts.size(); ++i) {
            std::string part_str(static_cast<char*>(parts[i].data()), parts[i].size());
            if (part_str == "<IDS|MSG>") {
                ++i;  // Skip the delimiter
                break;
            }
            identities.push_back(std::move(parts[i]));
            //std::cout << "Identities " << i << " " << part_str << "\n";
        }

        if (i + 4 > parts.size()) {
            std::cerr << "Malformed message: missing header parts" << std::endl;
            continue;
        } 

        // Now parse the remaining parts
        std::string signature = parts[i].to_string();
        std::string header_json = parts[i + 1].to_string();
        std::string parent_json = parts[i + 2].to_string();
        std::string metadata_json = parts[i + 3].to_string();
        std::string content_json = parts[i + 4].to_string();

        //std::cout <<"sig: " << signature << std::endl;
        //std::cout << "head :" <<header_json << std::endl;
        //std::cout << "parent: "<<parent_json << std::endl;
        //std::cout << "meta: "<<metadata_json << std::endl;
        //std::cout << "content :"<<content_json << std::endl;
        // Parse header and parent_header
        Parser p(header_json);
        JsonValue header = p.parse_value();
        std::string msg_type = header.o["msg_type"].s;
        std::string session = header.o["session"].s;

        Parser p_parent(parent_json);
        JsonValue parent_header = p_parent.parse_value();
        Parser p2(content_json);
        JsonValue content = p2.parse_value();

        send_status(iopub, "busy", session, key);
        //std::cout << "Message type :" << msg_type << std::endl;
        if (msg_type == "kernel_info_request") {
            //std::cout << "Sending kernel info reply" << std::endl;
            send_kernel_info_reply(shell, identities, key, session, header_json);
        }
        else if (msg_type == "history_request") {
            handle_history_request(content,
                header,
                identities,
                key,
                shell);
        }
        else if (msg_type == "comm_open") {
            handle_comm_open(content, header,identities,key,shell);
        }
        else if (msg_type == "comm_msg") {
            handle_comm_msg(content, header, identities, key, shell);
        }
        else if (msg_type == "comm_close") {
            handle_comm_close(content, header, identities);
        }
        else if (msg_type == "comm_info_request") {
            handle_comm_info_request(content, header, identities, key, shell);
        }
        else if (msg_type == "execute_request") {
            //std::string content_json = parts[6].to_string();
            if (!content.o["silent"].b && content.o["store_history"].b)
                exec_counter++;

            std::string code = content.o["code"].s;
            send_execute_input(code, exec_counter, header, identities, key, iopub);
            //std::cout << "received code: " << code << std::endl;
            size_t n_new_lines = 0;
            for (size_t i = 0; i < code.size(); i++)
                if (code[i] == '\n') n_new_lines++;

            //std::cout << "new lines in code " << n_new_lines << "\n";
                
            std::string ghci_result;
            if (n_new_lines == 0)
                ghci_result = ghci.send(code);
            else
                ghci_result = ghci.send_file_load(code);

            //std::cout << "ghci result: "<< ghci_result << std::endl;

            send_execute_result(iopub, identities, header, ghci_result, exec_counter, key);
            send_execute_reply(exec_counter, header, identities, key, shell);
        }
        else {
            std::cout << " --------------------------------------------------\n";
            std::cout << "Unhandled message type " << msg_type << std::endl;
            std::cout << " --------------------------------------------------\n";
            exit(1);
        }

            send_status(iopub, "idle", session, key);
        }

    ghci.stop();
    return 0;
}
