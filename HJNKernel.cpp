// main.cpp

#define _WINSOCKAPI_
#define WIN32_LEAN_AND_MEAN

#include "json_parser.hpp"
#include "ghci_bridge.hpp"
#include "sha256.hpp"
#include "jp_comm.hpp"
#include "jupyter_protocol.hpp"
#include "jp_history.hpp"
#include "jp_exec.hpp"

size_t exec_counter;

int main(int argc, char* argv[]) {
    exec_counter = 0;

    if (argc < 2) {
        std::cerr << "Usage: haskell_kernel.exe connection.json\n";
        return 1;
    }

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

    GHCiBridge ghci;
    ghci.start();

    while (true) {
        std::vector<zmq::message_t> parts;
        bool got_message = false;

        zmq::pollitem_t items[] = {
            { static_cast<void*>(shell), 0, ZMQ_POLLIN, 0 }
        };

        int rc = zmq::poll(items, 1, std::chrono::milliseconds(1000));

        if (rc == -1) {
            continue;
        }

        if (!(items[0].revents & ZMQ_POLLIN)) {
            continue;
        }

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


        if (!got_message) continue;
        if (parts.size() < 6) {
            std::cerr << "Incomplete message received: parts=" << parts.size() << std::endl;
            continue;
        }

        // Extract identities (everything before "<IDS|MSG>")
        size_t i = 0;
        std::vector<zmq::message_t> identities;
        for (; i < parts.size(); ++i) {
            std::string part_str(static_cast<char*>(parts[i].data()), parts[i].size());
            if (part_str == "<IDS|MSG>") {
                ++i;
                break;
            }
            identities.push_back(std::move(parts[i]));
        }

        if (i + 4 > parts.size()) {
            std::cerr << "Malformed message: missing header parts" << std::endl;
            continue;
        } 

        std::string signature = parts[i].to_string();
        std::string header_json = parts[i + 1].to_string();
        std::string parent_json = parts[i + 2].to_string();
        std::string metadata_json = parts[i + 3].to_string();
        std::string content_json = parts[i + 4].to_string();

        Parser p(header_json);
        JsonValue header = p.parse_value();
        std::string msg_type = header.o["msg_type"].s;
        std::string session = header.o["session"].s;

        Parser p_parent(parent_json);
        JsonValue parent_header = p_parent.parse_value();
        Parser p2(content_json);
        JsonValue content = p2.parse_value();

        send_status(iopub, "busy", session, key);

        if (msg_type == "kernel_info_request") {
            send_kernel_info_reply(shell, identities, key, session, header);
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
            if (!content.o["silent"].b && content.o["store_history"].b)
                exec_counter++;

            std::string code = content.o["code"].s;
            send_execute_input(code, exec_counter, header, identities, key, iopub);
            std::string ghci_result;
            ghci_result = ghci.send(code);
            /*
            size_t n_new_lines = 0;
            for (size_t i = 0; i < code.size(); i++)
                if (code[i] == '\n') n_new_lines++;
                
            std::string ghci_result;
            if (n_new_lines == 0)
                ghci_result = ghci.send(code);
            else
                ghci_result = ghci.send_file_load(code);*/

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
