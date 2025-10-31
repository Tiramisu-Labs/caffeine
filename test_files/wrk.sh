#!/bin/bash

printf "HTTP/1.1 200 OK\r\n"
printf "Content-Type: application/json\r\n"
printf "Content-Length: %d\r\n" "0"
printf "Connection: close\r\n"
printf "\r\n"
exit 0