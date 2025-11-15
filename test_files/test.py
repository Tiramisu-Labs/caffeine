import os
import sys
import json
import numpy

method = os.environ.get("REQUEST_METHOD", "GET")
query_string = os.environ.get("QUERY_STRING", "")
content_length = int(os.environ.get("CONTENT_LENGTH", 0))

request_body = ""
if content_length > 0:
    request_body = sys.stdin.read(content_length)

response_data = {
    "status": "success",
    "method_used": method,
    "query": query_string,
    "message": "Hello from Python!",
    "body_received": request_body[:50] + "..." if len(request_body) > 50 else request_body
}

print("HTTP/1.1 200 OK")
print("Content-Type: application/json")
print() # blank line separates headers from the body

print(json.dumps(response_data))