import json, uuid, hmac, hashlib
import zmq
from datetime import datetime
import socket

def load_connection_file(path):
    with open(path) as f:
        return json.load(f)

def sign(msg_list, key, signature_scheme):
    """Sign the list of JSON parts according to Jupyter protocol."""
    h = hmac.new(key.encode(), digestmod=hashlib.sha256)
    for m in msg_list:
        h.update(m)
    return h.hexdigest().encode()

def build_msg(msg_type, content, session=None, username="user", parent_header={}):
    """Create a complete Jupyter message parts list (without signature)."""
    if session is None:
        session = str(uuid.uuid4())
    header = {
        "msg_id": str(uuid.uuid4()),
        "username": username,
        "session": session,
        "msg_type": msg_type,
        "version": "5.3",
        "date": datetime.utcnow().isoformat() + "Z"
    }
    return (
        json.dumps(header).encode(),
        json.dumps(parent_header).encode(),
        json.dumps({}, ensure_ascii=False).encode(),
        json.dumps(content).encode()
    )

def connect_shell(conn_info, sock_type = zmq.DEALER, port_name = 'shell_port'):
    ctx = zmq.Context()
    sock = ctx.socket(sock_type)
    sock.linger = 1000
    sock.connect(f"tcp://{conn_info['ip']}:{conn_info[port_name]}")
    return sock

def free_port():
    s = socket.socket()
    s.bind(('', 0))
    port = s.getsockname()[1]
    s.close()
    return port

def generate_connection_file(path):
    ports = {
        "shell_port": free_port(),
        "iopub_port": free_port(),
        "stdin_port": free_port(),
        "control_port": free_port(),
        "hb_port": free_port(),
    }

    conn_info = {
        **ports,
        "ip": "127.0.0.1",
        "key": str(uuid.uuid4()),
        "transport": "tcp",
        "signature_scheme": "hmac-sha256",
        "kernel_name": "custom_test_kernel",
        "jupyter_session": "manual-test"
    }

    with open(path, "w") as f:
        json.dump(conn_info, f, indent=2)
    print(f"[kernel_conn] Wrote connection file to {path}")
    return conn_info
