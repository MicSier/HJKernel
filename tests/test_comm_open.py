from common import load_connection_file, connect_shell, build_msg, sign
import sys, uuid

def run_test(conn_file):
    conn_info = load_connection_file(conn_file)
    sock = connect_shell(conn_info)
    
    content = {
        "comm_id": str(uuid.uuid4()),
        "target_name": "my_comm",
        "data": {}
    }
    
    header, parent, meta, content_bin = build_msg("comm_open", content)
    signature = sign([header, parent, meta, content_bin], conn_info["key"], conn_info["signature_scheme"])
    
    sock.send_multipart([b"<IDS|MSG>", signature, header, parent, meta, content_bin])