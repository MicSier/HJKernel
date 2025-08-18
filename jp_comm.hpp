#ifndef COMM_HPP
#define COMM_HPP

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

#include "json_parser.hpp"
#include "sha256.hpp"
#include "jupyter_protocol.hpp"
#include "jp_history.hpp"


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

    auto it = comm_targets.find(target_name);
    if (it != comm_targets.end()) {
        it->second(comm_id, init_data);
    }
}

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

    std::cerr << "[COMM] Data for comm_id=" << comm_id
        << " = " << data.to_string() << "\n";

    JsonValue content;
    content.type = JsonValue::Object;
    content.o["comm_id"] = JsonValue{ JsonValue::String, false, 0.0, comm_id };
    content.o["data"] = data; // send back exactly what we got

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

    if (!comm_target_exists(target_name))
    {
        JsonValue close_content;
        close_content.type = JsonValue::Object;
        close_content.o["comm_id"] = JsonValue{ JsonValue::String, false, 0.0, comm_id };
        close_content.o["data"] = JsonValue{ JsonValue::Object }; // empty dict

        send_message("comm_close", close_content, parent_header, identities, key, socket);
        return;
    }

    create_comm_instance(comm_id, target_name, data);
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
        JsonValue close_content;
        close_content.type = JsonValue::Object;
        close_content.o["comm_id"] = JsonValue{ JsonValue::String, false, 0.0, comm_id };
        close_content.o["data"] = JsonValue{ JsonValue::Object };
        send_message("comm_close", close_content, parent_header, identities, key, socket);
        return;
    }

    handle_comm_data(comm_id, data, parent_header, identities, key, socket);
}

void handle_comm_close(const JsonValue& content,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities)
{
    std::string comm_id = content.o.at("comm_id").s;

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
    std::string target_name;
    if (content.o.count("target_name")) {
        target_name = content.o.at("target_name").s;
    }

    JsonValue comms(JsonValue::Object);

    for (const auto& kv : active_comms) {
        const std::string& comm_id = kv.first;
        const CommInstance inst = kv.second;

        if (target_name.empty() || inst.target_name == target_name) {
            JsonValue comm_info(JsonValue::Object);
            comm_info.o["target_name"] = JsonValue{ JsonValue::String, false, 0.0, inst.target_name };
            comms.o[comm_id] = comm_info;
        }
    }

    JsonValue reply_content(JsonValue::Object);
    reply_content.o["status"] = JsonValue{ JsonValue::String, false, 0.0, "ok" };
    reply_content.o["comms"] = comms;

    send_message("comm_info_reply", reply_content, parent_header, identities, key, socket);
}
#endif // COMM_HPP