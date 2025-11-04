import os
import json

obj = {}

for key, value in os.environ.items():
    obj[key] = value

response_data = {
    "status": "success",
    'env': obj
}

to_str = json.dumps(response_data)
print("HTTP/1.1 200 OK")
print("Content-Type: application/json")
print("Connection: close")
print(f"Content-Length: {len(to_str)}")
print() # blank line separates headers from the body

print(to_str)