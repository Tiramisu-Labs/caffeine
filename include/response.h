#ifndef RESPONSE_H
#define RESPONSE_H

#define FORBIDDEN       \
    "HTTP/1.1 403 Forbidden\r\n"            \
    "Content-Type: text/html\r\n"           \
    "Content-Length: 69\r\n"                \
    "\r\n"                                  \
    "<html>\n"                              \
    "    <body>\n"                          \
    "        <h1>403 Forbidden</h1>\n"      \
    "    </body>\n"                         \
    "</html>\n"
#define FORBIDDEN_LEN 140


#define TOO_LONG        \
    "HTTP/1.1 414 URI Too Long\r\n"         \
    "Content-Type: text/html\r\n"           \
    "Content-Length: 68\r\n"                \
    "\r\n"                                  \
    "<html>\n"                              \
    "    <body>\n"                          \
    "        <h1>414 Too Long</h1>\n"       \
    "    </body>\n"                         \
    "</html>\n"                             
#define TOO_LONG_LEN 143

#define BAD_REQUEST     \
    "HTTP/1.1 400 Bad Request\r\n"          \
    "Content-Type: text/html\r\n"           \
    "Content-Length: 71\r\n"                \
    "\r\n"                                  \
    "<html>\n"                              \
    "    <body>\n"                          \
    "        <h1>400 Bad Request</h1>\n"    \
    "    </body>\n"                         \
    "</html>\n"                             
#define BAD_REQUEST_LEN 145

#define NOT_FOUND       \
    "HTTP/1.1 404 Not Found\r\n"            \
    "Content-Type: text/html\r\n"           \
    "Content-Length: 69\r\n"                \
    "\r\n"                                  \
    "<html>\n"                              \
    "    <body>\n"                          \
    "        <h1>404 Not Found</h1>\n"      \
    "    </body>\n"                         \
    "</html>\n"
#define NOT_FOUND_LEN 140

#define INTERNAL_ERROR  \
    "HTTP/1.1 500 Internal Server Error\r\n"\
    "Content-Type: text/html\r\n"           \
    "Content-Length: 81\r\n"                \
    "\r\n"                                  \
    "<html>\n"                              \
    "    <body>\n"                          \
    "        <h1>500 Internal Server Error</h1>\n"      \
    "    </body>\n"                         \
    "</html>\n"
#define INTERNAL_ERROR_LEN 164

#endif