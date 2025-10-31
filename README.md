# **Caffeine\!**

This is a high-performance web server designed to handle $\\text{HTTP}$ requests and execute a specified binary or script handler in a secure and efficient manner. The architecture is built to be lightweight and scalable, leveraging Unix-like operating system features to manage concurrency without relying on complex, multi-threaded frameworks.

## **Architecture Overview**

The server follows a producer-consumer model with a **dynamic process pool**, a highly efficient and robust design for this type of task. It consists of two main components:

### **The Parent Process (Producer)**

This is the server's main listener. Its sole job is to accept new client connections on a standard $\\text{TCP}$ port (e.g., 8080). When a connection arrives, it uses the **poll()** system call to efficiently find the first available worker process to handle the request. The parent's code remains minimal and non-blocking on the web listener, ensuring it can handle many concurrent connections without getting bogged down by reading data or executing handlers.

### **The Worker Processes (Consumers)**

A pool of pre-forked worker processes, managed by the parent. Each worker operates in a loop:

1. It receives a file descriptor from the parent.  
2. It reads the full $\\text{HTTP}$ request and performs security/routing checks.  
3. It executes the specified handler (script or binary) in a new **disposable grandchild process**.  
4. It closes the client file descriptor and the input pipe, and immediately proceeds to the next step. The worker uses **signal(SIGCHLD, SIG\_IGN)** to instruct the operating system kernel to automatically clean up the handler process upon exit, ensuring the worker is **never blocked** waiting for handler execution.  
5. It sends a signal back to the parent indicating it is ready for the next request.

This architecture is secure because the potentially dangerous execve call is isolated within a disposable "grandchild" process. A crash in the executed handler will not affect the parent or other workers.

## **How It Works: The Communication Flow**

The key to this design is the use of Unix Domain Sockets for Inter-Process Communication ($\\text{IPC}$) and the Ready-Worker Queue system for load balancing.

### **Centralized Task Dispatch**

The parent process creates and binds to a single **Unix Domain Socket** file on the filesystem. All worker processes connect to this socket, establishing a dedicated, bidirectional channel with the parent.

### **Dynamic Load Balancing with poll()**

The parent does not use a simple Round-Robin system. Instead, the load balancing is **dynamic and event-driven**:

1. **Worker Readiness**: After a worker finishes processing a request, it writes a single "ready" byte back to the parent over its dedicated $\\text{IPC}$ channel.  
2. **Parent Polling**: The parent process uses the **poll()** system call to monitor all worker $\\text{IPC}$ channels for this "ready" byte. poll() blocks the parent until at least one worker is available.  
3. **File Descriptor Passing**: When a new $\\text{HTTP}$ request arrives (via accept()), the parent uses poll() to find the first worker that has signaled readiness, reads the signal byte, and then uses **sendmsg()** to pass the client's file descriptor to that specific worker.

This dynamic system ensures that the client request is always dispatched to the least busy available worker, minimizing latency and maximizing the utilization of the entire process pool.

### **Direct Communication**

Once a worker receives the file descriptor, it has a **direct connection to the client**. The executed handler streams its $\\text{HTTP}$ response **directly** to this file descriptor. This is achieved by the worker setting the handler's $\\text{STDOUT}$ to the client socket using dup2(). This mechanism maximizes performance by ensuring the worker process doesn't wait for any data transfer to finish.

## **Key Feature: Handler Execution and Multi-Language Support**

Caffeine operates as a high-performance $\\text{CGI}$ **(Common Gateway Interface) executor**. The Worker process sets up environment variables (like $\\text{REQUEST\\\_METHOD}$ and $\\text{QUERY\\\_STRING}$) and redirects the client socket directly to the handler's standard output ($\\text{STDOUT}$).

### **Handler Responsibility (The $\\text{CGI}$ Protocol)**

Since the handler's $\\text{STDOUT}$ is streamed directly to the client, the handler is **fully responsible** for generating a complete, valid $\\text{HTTP}$ response. This means the output **MUST** begin with the $\\text{HTTP/1.1}$ status line, followed by headers and the body.

### **Robust Error Handling**

To improve reliability, the Worker distinguishes between handler failures:

* **404 Not Found:** If the requested handler file is **missing** on disk, the server immediately responds with 404 Not Found without ever forking a handler process.  
* **500 Internal Server Error:** This status is returned only if the worker process itself encounters an error before or during the handler launch (e.g., fork or execlp fails, or pipes cannot be created). **Important Note:** Because the worker is non-blocking and does not call waitpid(), it **cannot detect** if the handler script (e.g., Python, $\\text{PHP}$) crashes after a successful launch. Runtime script errors will simply result in the client connection being closed abruptly.

## **Configuration and Management**

Caffeine supports running multiple instances simultaneously, each uniquely identified by a name.

### **Instance Control (Mandatory for Daemon/Log Operations)**

| Option | Shorthand | Description | Required? |
| :---- | :---- | :---- | :---- |
| **\--name** | \-n \<name\> | Specifies a unique instance name (e.g., api\_v1). | **MANDATORY** for \-d, \-s, \-l, and \--reset-logs. |
| **\--daemon** | \-d | Runs the specified instance in the background. | Requires \-n. |
| **\--stop** | \-s | Sends $\\text{SIGTERM}$ to stop the specified instance. | Requires \-n. |
| **\--list-instances** | \-L | Show all running instances (by scanning $\\text{PID}$ files). | No. |

### **Core Server Settings**

| Option | Shorthand | Default Value | Description |
| :---- | :---- | :---- | :---- |
| **\--path** | N/A | \~/.config/caffeine/ | The base directory where the server looks for handler executables. |
| **\--port** | \-p \<port\> | 8080 | Set the listening port. |
| **\--workers** | \-w \<num\> | 4 | Set the number of worker processes to fork and manage. |

### **Logging & Utilities**

| Option | Shorthand | Default Value | Description |
| :---- | :---- | :---- | :---- |
| **\--log** | \-l | N/A | **Displays the full log file for the instance specified by \-n.** |
| **\--log-level** | N/A | INFO | Set logging verbosity (DEBUG, INFO, WARN, ERROR). |
| **\--reset-logs** | N/A | N/A | Clear the log file for the specified instance. |
| **\--config** | \-c \<file\> | N/A | Load configuration from the specified file path (overridden by command-line options). |
| **\--help** | \-h | N/A | Display the help message and exit. |

### **Running as a Daemon**

To run Caffeine as a robust background service, you **must** provide a unique instance name:

1. Use the \--daemon or \-d flag **along with** \-n \<name\>.  
2. The parent process will initialize, fork the workers, and then **exit immediately**.  
3. The server continues running in the background, fully detached from your terminal session.  
4. The primary daemon's $\\text{PID}$ is recorded in the instance-specific file: /tmp/caffeine\_\<name\>.pid.

**Example:**

caffeine \-d \-n api\_service \--port 8081 \--workers 8

### **Managing the Daemon**

To manage an instance, you must refer to it by its name (-n).

* **View Logs:** caffeine \-l \-n api\_service  
* **Stop/Kill:** caffeine \-s \-n api\_service

### **Logging Paths**

All server output is directed to a log file, which is automatically created based on the instance name:  
Log Path: $HOME/.local/share/caffeine/\<instance\_name\>.log

## **Getting Started**

### **Prerequisites**

You'll need a $\\text{C}$ compiler (like gcc or clang) and a Unix-like operating system (Linux, macOS, etc.).

### **Build and Run**

* Clone this repository (or save the provided code files)  
* Make build.sh executable:  
  chmod \+x build.sh

* Launch the script and follow the instructions:  
  ./build.sh  
  ...  
  cd build  
  make install

* By default, the script will install the binary in $HOME/.local/bin. If you want to specify a different path, simply pass it to the script:  
  ./build.sh /usr/local

* Now you can run the server using:  
  caffeine

* To run it in the foreground for testing:  
  caffeine \--port 8080 \--workers 4  