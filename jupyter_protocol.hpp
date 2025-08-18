#ifndef UTILS_HPP
#define UTILS_HPP

//#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <zmq.hpp>
#include "json_parser.hpp"
#include "sha256.hpp"
#include <random>

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

/* not cross-platform and not needed now
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
*/

std::string iso8601_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    auto duration = now.time_since_epoch();
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration) % 1000000;

    struct tm utc_tm;
    if (gmtime_s(&utc_tm, &t) != 0) {
        return "";
    }

    std::ostringstream ss;
    ss << std::put_time(&utc_tm, "%FT%T");
    ss << "." << std::setfill('0') << std::setw(6) << microseconds.count() << "Z";
    return ss.str();
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
        socket.send(std::move(msg), zmq::send_flags::sndmore);
    }

    const std::string delimiter = "<IDS|MSG>";
    socket.send(zmq::message_t(delimiter.data(), delimiter.size()), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(sig), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(header_json), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(parent_json), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(meta_json), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(content_json), zmq::send_flags::none);
}

void send_status(zmq::socket_t& iopub_sock, const std::string& execution_state, const std::string& session, const std::string& key) {

    JsonValue content;
    content.type = JsonValue::Object;
    content.o["execution_state"] = JsonValue{ JsonValue::String, false, 0.0, execution_state };

    JsonValue parent_header;
    parent_header.type = JsonValue::Object;

    // IOPub uses topic as first frame
    std::string topic = "status";
    std::vector<zmq::message_t> identities;
    zmq::message_t topic_message(topic.begin(),topic.end());
    identities.push_back(std::move(topic_message));

    send_message("status", content, parent_header, identities, key, iopub_sock);
}

void send_kernel_info_reply(zmq::socket_t& sock,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    const std::string& session,
    const JsonValue& parent_header)
{
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

    JsonValue help_links;
    help_links.type = JsonValue::Array;

    JsonValue help_link;
    help_link.type = JsonValue::Object;
    help_link.o["text"] = JsonValue{ JsonValue::String, false, 0.0, "Help" };
    help_link.o["url"] = JsonValue{ JsonValue::String, false, 0.0, "https://example.com" };

    help_links.a.push_back(help_link);
    content.o["help_links"] = help_links;

    content.o["debugger"] = JsonValue{ JsonValue::Bool, false, 0.0, "" };
    send_message("status", content, parent_header, identities, key, sock);
}

#endif // UTILS_HPP