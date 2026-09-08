#ifndef RESPONSE_H
#define RESPONSE_H

#define FORBIDDEN \
    "HTTP/1.1 403 Forbidden\r\n" \
    "Content-Type: text/html\r\n" \
    "Content-Length: 69\r\n" \
    "\r\n" \
    "<html>\n" \
    "    <body>\n" \
    "        <h1>403 Forbidden</h1>\n" \
    "    </body>\n" \
    "</html>\n"

#define FORBIDDEN_LEN (sizeof(FORBIDDEN) - 1)


#define URI_TOO_LONG \
    "HTTP/1.1 414 URI Too Long\r\n" \
    "Content-Type: text/html\r\n" \
    "Content-Length: 68\r\n" \
    "\r\n" \
    "<html>\n" \
    "    <body>\n" \
    "        <h1>414 Too Long</h1>\n" \
    "    </body>\n" \
    "</html>\n"

#define URI_TOO_LONG_LEN (sizeof(URI_TOO_LONG) - 1)


#define BAD_REQUEST \
    "HTTP/1.1 400 Bad Request\r\n" \
    "Content-Type: text/html\r\n" \
    "Content-Length: 71\r\n" \
    "\r\n" \
    "<html>\n" \
    "    <body>\n" \
    "        <h1>400 Bad Request</h1>\n" \
    "    </body>\n" \
    "</html>\n"

#define BAD_REQUEST_LEN (sizeof(BAD_REQUEST) - 1)


#define NOT_FOUND \
    "HTTP/1.1 404 Not Found\r\n" \
    "Content-Type: text/html\r\n" \
    "Content-Length: 69\r\n" \
    "\r\n" \
    "<html>\n" \
    "    <body>\n" \
    "        <h1>404 Not Found</h1>\n" \
    "    </body>\n" \
    "</html>\n"

#define NOT_FOUND_LEN (sizeof(NOT_FOUND) - 1)


#define INTERNAL_ERROR \
    "HTTP/1.1 500 Internal Server Error\r\n" \
    "Content-Type: text/html\r\n" \
    "Content-Length: 81\r\n" \
    "\r\n" \
    "<html>\n" \
    "    <body>\n" \
    "        <h1>500 Internal Server Error</h1>\n" \
    "    </body>\n" \
    "</html>\n"

#define INTERNAL_ERROR_LEN (sizeof(INTERNAL_ERROR) - 1)


#define REQUEST_TIMEOUT \
    "HTTP/1.1 408 Request Timeout\r\n" \
    "Content-Type: text/html\r\n" \
    "Content-Length: 67\r\n" \
    "\r\n" \
    "<html>\n" \
    "    <body>\n" \
    "        <h1>408 Timeout</h1>\n" \
    "    </body>\n" \
    "</html>\n"

#define REQUEST_TIMEOUT_LEN (sizeof(REQUEST_TIMEOUT) - 1)

#endif