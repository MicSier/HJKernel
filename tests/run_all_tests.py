import subprocess
import time
import sys
from pathlib import Path
from common import generate_connection_file
import test_kernel_info_request
import test_execute_request
import test_history_request
import test_comm_open
import test_comm_msg
import test_comm_close

def main():
    conn_file = Path("kernel-test.json")
    
    # Step 1: Generate kernel-test.json
    generate_connection_file(conn_file)
    
    # Step 2: Start the kernel process
    # NOTE: adjust `./HJNKernel.exe` and args to your build output
    kernel_proc = subprocess.Popen(
        ["../x64/Debug/HJNKernel.exe", str(conn_file)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    
    try:
        # Give kernel a moment to bind ports
        time.sleep(10)
        
        # Step 3: Run tests
        print("=== Running kernel_info test ===")
        test_kernel_info_request.run_test(conn_file)
        
        print("=== Running execute test ===")
        test_execute_request.run_test(conn_file)
        
        print("=== Running history test ===")
        test_history_request.run_test(conn_file)
        
        print("=== Running comm test ===")
        test_comm_open.run_test(conn_file)
        test_comm_msg.run_test(conn_file)
        test_comm_close.run_test(conn_file)
        
        print("\n All tests finished.")
        
    finally:
        # Kill kernel process after tests
        kernel_proc.terminate()
        try:
            kernel_proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            kernel_proc.kill()

if __name__ == "__main__":
    main()
