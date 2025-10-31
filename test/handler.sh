#!/bin/bash

# --- 1. Get CGI Environment Data ---
METHOD="${REQUEST_METHOD:-GET}"
QUERY="${QUERY_STRING:-}"
CONTENT_LEN="${CONTENT_LENGTH:-0}"

# --- 2. Read Request Body (if present) ---
REQUEST_BODY=""
if [ "$CONTENT_LEN" -gt 0 ]; then
    # Read CONTENT_LENGTH bytes from stdin
    REQUEST_BODY=$(head -c "$CONTENT_LEN")
fi

# --- 3. Construct JSON Body ---
# Using printf to construct the JSON payload cleanly
RESPONSE_BODY=$(cat <<EOF
{
    "status": "success",
    "handler_type": "Bash Shell Script",
    "method_used": "$METHOD",
    "query_string": "$QUERY",
    "body_length": "$CONTENT_LEN",
    "first_10_body_chars": "$(echo "$REQUEST_BODY" | cut -c 1-10)"
}
EOF
)

# --- 4. Send HTTP Response (MUST be explicitly printed to stdout) ---

# CRITICAL FIX: Use a single, explicit printf call for all headers to ensure 
# correct CRLF (\r\n) termination without any accidental extra newlines.

printf "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"

# Print the JSON body
printf "%s\n" "$RESPONSE_BODY"
