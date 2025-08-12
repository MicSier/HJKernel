from common import load_connection_file, connect_shell, build_msg, sign
import sys

def run_test(conn_file):
    conn_info = load_connection_file(conn_file)
    sock = connect_shell(conn_info)
    
    comm_id = sys.argv[2] if len(sys.argv) > 2 else "PUT_EXISTING_COMM_ID"
    
    content = {
        "comm_id": comm_id,
        "data": {"test": "hello from comm_msg"}
    }
    
    header, parent, meta, content_bin = build_msg("comm_msg", content)
    signature = sign([header, parent, meta, content_bin], conn_info["key"], conn_info["signature_scheme"])
    
    sock.send_multipart([b"<IDS|MSG>", signature, header, parent, meta, content_bin])