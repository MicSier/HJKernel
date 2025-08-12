from common import load_connection_file, connect_shell, build_msg, sign
import sys
import zmq

def run_test(conn_file):
    conn_info = load_connection_file(conn_file)
    sock = connect_shell(conn_info)

    # Build message
    header, parent, meta, content = build_msg("kernel_info_request", {})
    # Sign
    signature = sign([header, parent, meta, content], conn_info["key"], conn_info["signature_scheme"])
    # Send
    sock.send_multipart([b"<IDS|MSG>", signature, header, parent, meta, content])
    # Receive reply
    sock.RCVTIMEO = 2000  # 2 seconds
    try:
        parts = sock.recv_multipart()
        print(parts)
    except zmq.Again:
        print("No message received within timeout")