The execve Web Server 🚀
This is a high-performance web server designed to handle HTTP requests and execute a specified binary file in a secure and efficient manner. The architecture is built to be lightweight and scalable, leveraging Unix-like operating system features to manage concurrency without relying on complex, multi-threaded frameworks.

⚙️ Architecture Overview
The server follows a producer-consumer model with a process pool, a highly efficient and robust design for this type of task. It consists of two main components:

The Parent Process (Producer): This is the server's main listener. Its sole job is to accept new client connections on a standard TCP port (e.g., 8080). When a connection arrives, it gets the client's file descriptor and sends it to a worker process. The parent's code is minimal and non-blocking, ensuring it can handle many concurrent connections without getting bogged down by reading data or executing binaries.

The Worker Processes (Consumers): A pool of pre-forked worker processes, managed by the parent. Each worker waits to receive a file descriptor from the parent. When it gets one, it reads the full HTTP request (including large POST bodies), executes the specified binary in a new child process, and sends the binary's output directly back to the client. After the task is complete, the worker returns to its waiting state, ready for the next request.

This architecture is secure because the potentially dangerous execve call is isolated within a disposable "grandchild" process. A crash in the executed binary will not affect the parent or other workers.

🤝 How It Works: The Communication Flow
The key to this design is the use of Unix Domain Sockets for Inter-Process Communication (IPC).

Centralized Task Queue: The parent process creates and binds to a single Unix Domain Socket file on the filesystem (/tmp/webserver.sock). All worker processes connect to this same socket, forming a shared queue.

File Descriptor Passing: When a new HTTP request arrives, the parent accepts it, gets the client's file descriptor, and uses sendmsg() to pass the file descriptor to a worker over the Unix Domain Socket.

Kernel-level Load Balancing: All workers are in a blocking recvmsg() call, waiting for a file descriptor. The Linux kernel automatically handles the load balancing, ensuring that only one available worker receives the file descriptor. The parent doesn't need to track which worker is busy; it just passes the task on, and the kernel finds the first free worker.

Direct Communication: Once a worker receives the file descriptor, it now has a direct connection to the client. It handles the entire request-response cycle from that point on, keeping the parent process free to accept new connections.

📦 Getting Started
Prerequisites
You'll need a C compiler (like gcc) and a Unix-like operating system (Linux, macOS, etc.).

Build the Server
Clone this repository (or save the provided code files).

Compile the single source file into a single executable:

Bash

gcc server.c -o server
Run the Server
Start the parent process. It will automatically fork the worker pool and begin listening.

Bash

./server
The server is now running and listening on port 8080.

💻 Usage
You can test the server using curl or a web browser.

Simple Request
This will be handled directly by one of the worker processes, which will return a simple "Hello, world!" response.

Bash

curl http://localhost:8080
Executing a Binary
This will instruct a worker to execute a binary (/bin/ls -l / in the example) and send its output back to the client.

Bash

curl http://localhost:8080/run-binary
📝 Code Structure
common.h: Defines shared constants and utility functions for passing file descriptors.

server.c: The single source file containing all the logic for both the parent (listener) and worker processes.