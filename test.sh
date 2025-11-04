#!/bin/bash
# ----------------------------------------------------------------------
# CAFFEINE INTEGRATION TEST SUITE
# This script compiles a test handler, starts a Caffeine instance,
# sends a request, verifies the response, and cleans up all files.
# ----------------------------------------------------------------------

CAFFEINE_EXE="caffeine"
HANDLER_ECHO="echo"
HANDLER_BASH="handler.sh"
HANDLER_PYTHON="handler.py"
TEST_INSTANCE_NAME="integration_test"
TEST_PORT="8989"
TEST_URL="http://127.0.0.1:${TEST_PORT}/"

CAFFEINE_HOME="${HOME}/.config/caffeine"
PID_FILE="/tmp/caffeine_${TEST_INSTANCE_NAME}.pid"
LOG_FILE="${HOME}/.local/share/caffeine/${TEST_INSTANCE_NAME}.log"

cleanup() {
    echo -e "\n--- CLEANUP ---"
    
    if [ -f "$PID_FILE" ]; then
        echo "Stopping Caffeine instance ($TEST_INSTANCE_NAME)..."
        "$CAFFEINE_EXE" -s --name "$TEST_INSTANCE_NAME"
        sleep 1
    fi
    
    rm -f "$PID_FILE"
    rm -f "./${HANDLER_ECHO}"
    rm -f "$CAFFEINE_HOME/${HANDLER_ECHO}"
    
    echo "Cleanup complete."
}

trap cleanup EXIT

echo "--- 1. PREPARING TEST ENVIRONMENT ---"

if [ ! command -v "$CAFFEINE_EXE" ]; then
    echo "ERROR: Caffeine executable not found at '$CAFFEINE_EXE'. Please compile and ensure it's in the current directory."
    exit 1
fi

mkdir -p "$CAFFEINE_HOME"
echo "Target handler directory: $CAFFEINE_HOME"

if [ -f "$PID_FILE" ]; then
    echo "WARNING: Found existing PID file. Stopping old instance..."
    "$CAFFEINE_EXE" -s --name "$TEST_INSTANCE_NAME"
    sleep 1
fi

echo "--- 2. COMPILING AND DEPLOYING HANDLER ---"

gcc test_files/echo.c -o $HANDLER_ECHO
if [ $? -ne 0 ]; then
    echo "ERROR: Handler compilation failed."
    exit 1
fi

mv $HANDLER_ECHO "$CAFFEINE_HOME/"
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to move handler to $CAFFEINE_HOME. Check permissions."
    exit 1
fi
echo "Handler deployed to: $CAFFEINE_HOME/$HANDLER_ECHO"

cp test_files/$HANDLER_BASH "$CAFFEINE_HOME/"
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to move handler to $CAFFEINE_HOME. Check permissions."
    exit 1
fi
echo "Handler deployed to: $CAFFEINE_HOME/$HANDLER_BASH"


cp test_files/$HANDLER_PYTHON "$CAFFEINE_HOME/"
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to move handler to $CAFFEINE_HOME. Check permissions."
    exit 1
fi
echo "Handler deployed to: $CAFFEINE_HOME/$HANDLER_PYTHON"

echo "--- 3. STARTING CAFFEINE INSTANCE ---"

"$CAFFEINE_EXE" -D -n "$TEST_INSTANCE_NAME" -p "$TEST_PORT" -w 1 
if [ $? -ne 0 ]; then
    echo "FATAL ERROR: Caffeine failed to start."
    exit 1
fi
echo "Caffeine started as daemon. PID file: $PID_FILE"

sleep 2

echo "--- 4. EXECUTING HTTP TEST REQUEST ---"

echo "GET: $TEST_URL${HANDLER_ECHO}"
HTTP_RESPONSE_GET=$(curl -s --max-time 5 "$TEST_URL${HANDLER_ECHO}")
CURL_STATUS=$?


if [ $CURL_STATUS -ne 0 ]; then
    echo "ERROR: Curl failed to connect or timed out (Status: $CURL_STATUS)."
    echo "       Check log file: $LOG_FILE"
    exit 1
fi

echo "POST: $TEST_URL${HANDLER_ECHO}"
HTTP_RESPONSE_POST=$(curl -X POST -s --max-time 5 "$TEST_URL${HANDLER_ECHO}" -d "Caffeine handler executed successfully! Instance: integration_test (Method: GET)")
CURL_STATUS=$?

if [ $CURL_STATUS -ne 0 ]; then
    echo "ERROR: Curl failed to connect or timed out (Status: $CURL_STATUS)."
    echo "       Check log file: $LOG_FILE"
    exit 1
fi

echo "GET: $TEST_URL${HANDLER_BASH}"
HTTP_RESPONSE_BASH=$(curl -s --max-time 5 "$TEST_URL${HANDLER_BASH}")
CURL_STATUS=$?

if [ $CURL_STATUS -ne 0 ]; then
    echo "ERROR: Curl failed to connect or timed out (Status: $CURL_STATUS)."
    echo "       Check log file: $LOG_FILE"
    exit 1
fi

echo "GET: $TEST_URL${HANDLER_PYTHON}"
HTTP_RESPONSE_PYTHON=$(curl -s --max-time 5 "$TEST_URL${HANDLER_PYTHON}")
CURL_STATUS=$?

if [ $CURL_STATUS -ne 0 ]; then
    echo "ERROR: Curl failed to connect or timed out (Status: $CURL_STATUS)."
    echo "       Check log file: $LOG_FILE"
    exit 1
fi

echo "--- 5. VALIDATING RESPONSE ---"

GET_OUTPUT="Caffeine handler executed successfully! Instance: integration_test (Method: GET)"
if [[ "$HTTP_RESPONSE_GET" == *"$GET_OUTPUT"* ]]; then
    echo -e "\n✅ SUCCESS: Integration test passed!"
    echo "Received Body: $HTTP_RESPONSE_GET"
else
    echo -e "\n❌ FAILURE: Response mismatch."
    echo "Expected Body snippet: '$GET_OUTPUT'"
    echo "Received Body: '$HTTP_RESPONSE_GET'"
    exit 1
fi

POST_OUTPUT="echo: Caffeine handler executed successfully! Instance: integration_test (Method: GET)"
if [[ "$HTTP_RESPONSE_POST" == *"$POST_OUTPUT"* ]]; then
    echo -e "\n✅ SUCCESS: Integration test passed!"
    echo "Received Body: $HTTP_RESPONSE_POST"
else
    echo -e "\n❌ FAILURE: Response mismatch."
    echo "Expected Body snippet: '$POST_OUTPUT'"
    echo "Received Body: '$HTTP_RESPONSE_POST'"
    exit 1
fi

BASH_OUTPUT='{
    "status": "success",
    "handler_type": "Bash Shell Script",
    "method_used": "GET",
    "query_string": "",
    "body_length": "0",
    "first_10_body_chars": ""
}'

if [[ "$HTTP_RESPONSE_BASH" == *"$BASH_OUTPUT"* ]]; then
    echo -e "\n✅ SUCCESS: Integration test passed!"
    echo "Received Body: $HTTP_RESPONSE_BASH"
else
    echo -e "\n❌ FAILURE: Response mismatch."
    echo "Expected Body snippet: '$BASH_OUTPUT'"
    echo "Received Body: '$HTTP_RESPONSE_BASH'"
    exit 1
fi

PY_OUTPUT="{"status": "success", "method_used": "GET", "query": "", "message": "Hello from Python!", "body_received": ""}"
if [[ "$HTTP_RESPONSE_PYTHON" == *"$PY_OUTPUT"* ]]; then
    echo -e "\n✅ SUCCESS: Integration test passed!"
    echo "Received Body: $HTTP_RESPONSE_PYTHON"
else
    echo -e "\n❌ FAILURE: Response mismatch."
    echo "Expected Body snippet: '$PY_OUTPUT'"
    echo "Received Body: '$HTTP_RESPONSE_PYTHON'"
    exit 1
fi