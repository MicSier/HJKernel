from common import load_connection_file, connect_shell, build_msg, sign
import sys
import zmq

def run_test(conn_file):
    conn_info = load_connection_file(conn_file)
    sock_shell = connect_shell(conn_info)
    
    content = {
        "code": "print \"Hello from execute_request test\"",
        "silent": False,
        "store_history": True,
        "user_expressions": {},
        "allow_stdin": False,
        "stop_on_error": True
    }
    
    header, parent, meta, content_bin = build_msg("execute_request", content)
    signature = sign([header, parent, meta, content_bin], conn_info["key"], conn_info["signature_scheme"])
    
    sock_shell.send_multipart([b"<IDS|MSG>", signature, header, parent, meta, content_bin])
    #print("Sent message: ", signature, header, parent, meta, content_bin)
    sock_shell.RCVTIMEO = 10000  # 10 seconds
    try:
        parts = sock_shell.recv_multipart()
        print(parts)
    except zmq.Again:
        print("No message received within timeout")