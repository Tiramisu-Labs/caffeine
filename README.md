# Caffeine

Caffeine is a high-performance web server designed to handle HTTP requests and execute logic via dynamic library handlers. The architecture leverages the operating system's kernel for concurrent connection handling, ensuring high efficiency and low overhead.

---

## Architecture Overview: Shared Listener Model

Caffeine utilizes a **Shared Listener** model. This approach removes Inter-Process Communication (IPC) overhead by handling load balancing directly within the operating system kernel using the `SO_REUSEPORT` socket option.

### The Parent Process (Supervisor)

The parent process initializes the environment and supervises the worker pool. Its responsibilities include:

* **Initialization:** Creating and binding the single TCP listening socket and setting the `SO_REUSEPORT` option.
* **Worker Management:** Pre-forking the pool of worker processes.
* **Active Monitoring:** Monitoring workers for resource exhaustion or infinite loops.
* **Process Recovery:** Killing unresponsive workers and respawning them to maintain server stability.
* **Scaling:** Adjusting the number of workers dynamically based on incoming load.

The parent process does not accept client connections; it manages the lifecycle of the workers that do.

### The Worker Processes (Consumers)

A pool of pre-forked worker processes inherits the listening socket. Each worker operates independently:

* Each worker calls `accept()` on the shared socket.
* The kernel distributes new connections to one available worker.
* The worker handles the request, executes the specified dynamic library handler, and returns to the `accept()` loop.

---

## The Key: SO_REUSEPORT

`SO_REUSEPORT` allows multiple independent processes to bind to the same IP address and port.

* **Sharing:** All worker processes inherit and listen on the same port simultaneously.
* **Kernel Distribution:** The kernel's network stack uses an internal hash (based on client IP and port) to hand the connection to exactly one waiting worker.
* **Benefits:** This ensures high throughput, prevents the "thundering herd" problem, and provides excellent cache locality since no data is copied between processes for dispatching.

---

## Handler Execution (Dynamic Library Model)

Caffeine functions as a high-performance executor for dynamic libraries (`.so` files). This is more efficient than the traditional CGI model as it avoids the overhead of process forking for every request.

* **Internal Execution:** The worker loads the requested handler as a dynamic library into its own memory space.
* **Execution:** The worker sets up the environment and calls the handler's entry point.
* **Direct Response:** The handler generates the HTTP response, which is streamed directly back to the client socket.
* **Safety:** If a handler causes a worker to hang or consume excessive resources, the **Supervisor** detects the anomaly, kills the process, and respawns a clean worker.

### Handler Responsibility

The handler is fully responsible for generating a complete, valid HTTP response, which **must** begin with the HTTP/1.1 status line.

---

## Configuration and Management

### Instance Control

| Option | Shorthand | Description | Required? |
| --- | --- | --- | --- |
| --name | -n  | Unique instance name. | **Mandatory** for daemon, stop, and logs. |
| --daemon | -D | Runs the instance in the background. | Requires -n. |
| --stop | -s | Sends SIGTERM to stop the instance. | Requires -n. |
| --list-instances | -L | Lists all running instances. | No. |

### Core Server Settings

| Option | Shorthand | Default | Description |
| --- | --- | --- | --- |
| --path | N/A | ~/.config/caffeine/ | Base directory for handler .so files. |
| --port | -p  | 8080 | The listening port. |
| --workers | -w  | 4 | Number of worker processes to manage. |
| --config | -c  | N/A | Load configuration from a file. |

### Logging & Utilities

| Option | Shorthand | Description |
| --- | --- | --- |
| --log | -l | Displays the log file for the specified instance. |
| --log-level | N/A | Set verbosity (DEBUG, INFO, WARN, ERROR). |
| --reset-logs | N/A | Clears the log file for the specified instance. |
| --deploy | -d | Upload a file or directory to the execution path. |

---

## Running as a Daemon

To run in the background, use the `--daemon` or `-D` flag with a name. The parent process manages the background workers.

**Example:**
`caffeine -D -n api_service --port 8081 --workers 8`

**Management:**

* **View Logs:** `caffeine -l -n api_service`
* **Stop Instance:** `caffeine -s -n api_service`

---

## Getting Started

### Prerequisites

A C compiler (gcc or clang) and a Unix-like operating system that supports `SO_REUSEPORT`.

### Build and Run

1. **Prepare the build script:**
`chmod +x build.sh`
2. **Build and install:**
`./build.sh`
`cd build`
`make install`
3. **Run for testing:**
`caffeine --port 8080 --workers 4`