from common import load_connection_file, connect_shell, build_msg, sign
import sys

def run_test(conn_file):
    conn_info = load_connection_file(conn_file)
    sock = connect_shell(conn_info)
    
    content = {
        "output": False,
        "raw": True,
        "hist_access_type": "tail",
        "n": 5
    }
    
    header, parent, meta, content_bin = build_msg("history_request", content)
    signature = sign([header, parent, meta, content_bin], conn_info["key"], conn_info["signature_scheme"])
    
    sock.send_multipart([b"<IDS|MSG>", signature, header, parent, meta, content_bin])
    sock.RCVTIMEO = 2000  # 2 seconds
    try:
        parts = sock.recv_multipart()
        print(parts)
    except zmq.Again:
        print("No message received within timeout")