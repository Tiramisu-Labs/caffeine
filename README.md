# Caffeine!
This is a high-performance web server designed to handle HTTP requests and execute a specified binary file in a secure and efficient manner. The architecture is built to be lightweight and scalable, leveraging Unix-like operating system features to manage concurrency without relying on complex, multi-threaded frameworks.

## Architecture Overview
The server follows a producer-consumer model with a **dynamic process pool**, a highly efficient and robust design for this type of task. It consists of two main components:

The **Parent Process (Producer)**: This is the server's main listener. Its sole job is to accept new client connections on a standard TCP port (e.g., 8080). When a connection arrives, it uses the **poll()** system call to efficiently find the first available worker process to handle the request. The parent's code remains minimal and non-blocking on the web listener, ensuring it can handle many concurrent connections without getting bogged down by reading data or executing binaries.

The **Worker Processes (Consumers)**: A pool of pre-forked worker processes, managed by the parent. Each worker operates in a loop: it receives a file descriptor from the parent, reads the full HTTP request, executes the specified binary in a new child process, and sends the binary's output directly back to the client. After the task is complete, the worker **sends a signal back to the parent** indicating it is ready for the next request.

This architecture is secure because the potentially dangerous execve call is isolated within a disposable "grandchild" process. A crash in the executed binary will not affect the parent or other workers.

## How It Works: The Communication Flow
The key to this design is the use of Unix Domain Sockets for Inter-Process Communication (IPC) and the Ready-Worker Queue system for load balancing.

**Centralized Task Dispatch**: The parent process creates and binds to a single Unix Domain Socket file on the filesystem (/tmp/webserver.sock). All worker processes connect to this socket, establishing a dedicated, bidirectional channel with the parent.

**Dynamic Load Balancing with poll()**: The parent does not use a simple Round-Robin system. Instead, the load balancing is dynamic and event-driven:

 1. **Worker Readiness**: After a worker finishes processing a request, it writes a single "ready" byte back to the parent over its dedicated IPC channel.

2. **Parent Polling**: The parent process uses the poll() system call to monitor all worker IPC channels for this "ready" byte. poll() blocks the parent until at least one worker is available.

3. **File Descriptor Passing**: When a new HTTP request arrives (via accept()), the parent uses poll() to find the first worker that has signaled readiness, reads the signal byte, and then uses sendmsg() to pass the client's file descriptor to that specific worker.

This dynamic system ensures that the client request is always dispatched to the least busy available worker, minimizing latency and maximizing the utilization of the entire process pool, regardless of the variability in binary execution times.

**Direct Communication**: Once a worker receives the file descriptor, it now has a direct connection to the client. It handles the entire request-response cycle from that point on, keeping the parent process free to accept new connections.



## Getting Started
### Prerequisites
You'll need a C compiler (like gcc or clang) and a Unix-like operating system (Linux, macOS, etc.).

### Build and Run
- Clone this repository (or save the provided code files)
- make build.sh executable
```
chmod +x build.sh
```
- launch the script and follow the instructions
```
./build.sh
...
cd build
make install
```
By deault, the script will install the binary in $HOME/.local. If you want to specify a different path, simply pass it to the script:
```
./build.sh /usr/local
```
- now you can run the server using
```
./caffeine
```
By default the server listen on port 8080 and spawn 4 workers. Use --workers and -- port to modify this values