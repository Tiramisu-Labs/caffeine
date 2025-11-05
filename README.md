## **Caffeine\!**

This is a high-performance web server designed to handle **HTTP requests** and execute a specified binary or script handler. The architecture is built to be lightweight, efficient, and scalable, leveraging the operating system's kernel for concurrent connection handling.

---

## **Architecture Overview: Shared Listener Model**

Caffeine uses a **Shared Listener** model, which is highly efficient because it removes all Inter-Process Communication (IPC) overhead. The load-balancing is handled directly by the operating system kernel using the SO\_REUSEPORT socket option.

### **The Parent Process (Supervisor)**

The parent process acts as the initializer and supervisor. It performs three key tasks:

1. Create and bind the **single TCP listening socket**.
2. Set the critical **SO\_REUSEPORT** socket option on the listener.
3. **Pre-fork** the pool of worker processes.
4. Wait for termination signals to manage shutdown.

The parent process does **not** accept client connections or dispatch requests. It closes the listening file descriptor after forking, allowing workers to take over.

### **The Worker Processes (Consumers)**

A pool of pre-forked worker processes, each inheriting the **same listening socket** file descriptor from the parent. Each worker operates independently:

1. Each worker calls **accept()** on the shared listening socket.  
2. The kernel directly distributes new connections to one available worker.  
3. The worker handles the request, executes the handler, and returns to the accept() loop.

This eliminates the need for any custom, software-based load balancer or IPC queue.

---

## **The Key: SO\_REUSEPORT**

SO\_REUSEPORT is a socket option that allows **multiple independent sockets** (in this case, belonging to multiple worker processes) to be bound to the exact same $\\text{IP}$ address and port combination.

### **How it works:**

* **Sharing:** The parent sets this option, allowing all worker processes to inherit and listen on the same port simultaneously.  
* **Kernel Distribution:** When a new connection arrives, the kernel's network stack uses an internal distribution mechanism (often a hash based on the client's $\\text{IP}$ and port) to wake up and hand the connection's file descriptor to **only one** of the workers waiting on accept().  
* **Benefits:** This achieves high throughput, excellent cache locality (since no data is copied between processes for dispatch), and protects against the "thundering herd" problem where all waiting processes wake up for a single event.

---

## **Handler Execution (CGI Model)**

Caffeine operates as a high-performance **CGI (Common Gateway Interface) executor**.

* The worker reads the full $\\text{HTTP}$ request and sets up necessary environment variables.  
* It executes the requested handler (script or binary) in a new **disposable grandchild process**.  
* The worker uses $\\text{dup2()}$ to redirect the handler's **STDOUT directly to the client socket**, streaming the response back to the client.  
* The worker configures the kernel to automatically clean up the grandchild upon exit (SIGCHLD, SIG\\\_IGN), allowing the worker to immediately return to servicing new requests without waiting.

### **Handler Responsibility**

The handler is **fully responsible** for generating a complete, valid $\\text{HTTP}$ response, which **MUST** begin with the $\\text{HTTP/1.1}$ status line.

### **Error Handling**

* **404 Not Found:** Returned immediately if the handler file is missing.  
* **500 Internal Server Error:** Returned if the worker fails during handler launch (e.g., $\\text{fork}$ fails).  
* **Note:** The worker is non-blocking and cannot detect if the executed handler script crashes after a successful launch; such errors will result in the client connection being closed.

---

## **Configuration and Management**

Caffeine supports running multiple instances, each identified by a unique name.

### **Instance Control**

| Option | Shorthand | Description | Required? |
| :---- | :---- | :---- | :---- |
| **\--name** | \-n \<name\> | Specifies a unique instance name (**MANDATORY** for \-D, \-s, \-l, and \--reset-logs). | **MANDATORY** |
| **\--daemon** | \-D | Runs the specified instance in the background. | Requires \-n. |
| **\--stop** | \-s | Sends $\\text{SIGTERM}$ to stop the specified instance. | Requires \-n. |
| **\--list-instances** | \-L | Show all running instances (by scanning $\\text{PID}$ files). | No. |

### **Core Server Settings**

| Option | Shorthand | Default Value | Description |
| :---- | :---- | :---- | :---- |
| **\--path** | N/A | \~/.config/caffeine/ | The base directory for handler executables. |
| **\--port** | \-p \<port\> | 8080 | Set the listening port. |
| **\--workers** | \-w \<num\> | 4 | Set the number of worker processes to fork and manage. |

### **Content Deployment**

| Option | Shorthand | Default Value | Description |
| :---- | :---- | :---- | :---- |
| **\--deploy** | \-d | N/A | **Upload a file or directory to the server's execution path.** |

### **Logging & Utilities**

| Option | Shorthand | Default Value | Description |
| :---- | :---- | :---- | :---- |
| **\--log** | \-l | N/A | **Displays the full log file for the instance specified by \-n.** |
| **\--log-level** | N/A | INFO | Set logging verbosity (DEBUG, INFO, WARN, ERROR). |
| **\--reset-logs** | N/A | N/A | Clear the log file for the specified instance. |
| **\--config** | \-c \<file\> | N/A | Load configuration from the specified file path. |
| **\--help** | \-h | N/A | Display the help message and exit. |

### **Running as a Daemon**

To run in the background, use the $\\text{--daemon}$ or $\\text{-D}$ flag along with $\\text{-n \\. The parent process exits immediately after launching the workers.

Example:  
caffeine \-D \-n api\_service \--port 8081 \--workers 8  
**Management:**

* **View Logs:** caffeine \-l \-n api\_service  
* **Stop/Kill:** caffeine \-s \-n api\_service

---

## **Getting Started**

### **Prerequisites**

You need a $\\text{C}$ compiler ($\\text{gcc}$ or $\\text{clang}$) and a Unix-like operating system that supports **SO\_REUSEPORT**.

### **Build and Run**

* Make build.sh executable:
```
  chmod \+x build.sh
```
* Build and install:
```
  ./build.sh  
  ...  
  cd build  
  make install
```

* Run the server:
```
  caffeine
```

* Foreground testing:  
```
  caffeine \--port 8080 \--workers 4
```