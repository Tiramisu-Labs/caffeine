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
    "</html>\n"                             \


#define TOO_LONG        \
    "HTTP/1.1 414 URI Too Long\r\n"         \
    "Content-Type: text/html\r\n"           \
    "Content-Length: 68\r\n"                \
    "\r\n"                                  \
    "<html>\n"                              \
    "    <body>\n"                          \
    "        <h1>414 Too Long</h1>\n"       \
    "    </body>\n"                         \
    "</html>\n"                             \

#define BAD_REQUEST     \
    "HTTP/1.1 400 Bad Request\r\n"          \
    "Content-Type: text/html\r\n"           \
    "Content-Length: 71\r\n"                \
    "\r\n"                                  \
    "<html>\n"                              \
    "    <body>\n"                          \
    "        <h1>400 Bad Request</h1>\n"    \
    "    </body>\n"                         \
    "</html>\n"                             \

#define NOT_FOUND       \
    "HTTP/1.1 404 Not Found\r\n"            \
    "Content-Type: text/html\r\n"           \
    "Content-Length: 69\r\n"                \
    "\r\n"                                  \
    "<html>\n"                              \
    "    <body>\n"                          \
    "        <h1>404 Not Found</h1>\n"      \
    "    </body>\n"                         \
    "</html>\n"                             \

#endif