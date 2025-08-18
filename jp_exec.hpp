#ifndef EXEC_HPP
#define EXEC_HPP

#include "jupyter_protocol.hpp"

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

    JsonValue content(JsonValue::Object);
    content.o["execution_count"] = JsonValue{ JsonValue::Number, false, (double)execution_count, "" };

    JsonValue data(JsonValue::Object);
    data.o["text/plain"] = JsonValue{ JsonValue::String, false, 0.0, result };

    content.o["data"] = data;
    content.o["metadata"] = JsonValue(JsonValue::Object);

    send_message("execute_result", content, parent_header, identities, key, sock);
}

void send_execute_input(const std::string& code,
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

void send_execute_reply(int execution_count,
    const JsonValue& parent_header,
    const std::vector<zmq::message_t>& identities,
    const std::string& key,
    zmq::socket_t& socket) {

    JsonValue content;
    content.type = JsonValue::Object;
    content.o["status"] = JsonValue{ JsonValue::String, false, 0.0, "ok" };
    content.o["execution_count"] = JsonValue{ JsonValue::Number, false, (double)execution_count, "" };
    JsonValue expressions = JsonValue{ JsonValue::Object };
    content.o["user_expressions"] = expressions;
    content.o["payload"] = JsonValue{ JsonValue::Array, {} };

    send_message("execute_reply", content, parent_header, identities, key, socket);
}
#endif // EXEC_HPP