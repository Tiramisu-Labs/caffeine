#!/bin/bash
# ----------------------------------------------------------------------
# CAFFEINE INTEGRATION TEST SUITE
# This script compiles a test handler, starts a Caffeine instance,
# sends a request, verifies the response, and cleans up all files.
# ----------------------------------------------------------------------

CAFFEINE_EXE="./caffeine"
HANDLER_NAME="test_post"
TEST_INSTANCE_NAME="integration_test"
TEST_PORT="8080"
TEST_URL="http://127.0.0.1:${TEST_PORT}/${HANDLER_NAME}"
EXPECTED_OUTPUT="Caffeine handler executed successfully! Instance: integration_test"


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
    rm -f "./${HANDLER_NAME}"
    rm -f "$CAFFEINE_HOME/${HANDLER_NAME}"
    
    echo "Cleanup complete."
}

trap cleanup EXIT

echo "--- 1. PREPARING TEST ENVIRONMENT ---"

if [ ! -x "$CAFFEINE_EXE" ]; then
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

gcc test_handler.c -o $HANDLER_NAME 
if [ $? -ne 0 ]; then
    echo "ERROR: Handler compilation failed."
    exit 1
fi

mv $HANDLER_NAME "$CAFFEINE_HOME/"
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to move handler to $CAFFEINE_HOME. Check permissions."
    exit 1
fi
echo "Handler deployed to: $CAFFEINE_HOME/$HANDLER_NAME"

echo "--- 3. STARTING CAFFEINE INSTANCE ---"

"$CAFFEINE_EXE" -d -n "$TEST_INSTANCE_NAME" -p "$TEST_PORT" -w 1 
if [ $? -ne 0 ]; then
    echo "FATAL ERROR: Caffeine failed to start."
    exit 1
fi
echo "Caffeine started as daemon. PID file: $PID_FILE"

sleep 2

echo "--- 4. EXECUTING HTTP TEST REQUEST ---"

echo "Requesting: $TEST_URL"
HTTP_RESPONSE=$(curl -s --max-time 5 "$TEST_URL")
CURL_STATUS=$?

if [ $CURL_STATUS -ne 0 ]; then
    echo "ERROR: Curl failed to connect or timed out (Status: $CURL_STATUS)."
    echo "       Check log file: $LOG_FILE"
    exit 1
fi

echo "--- 5. VALIDATING RESPONSE ---"

if [[ "$HTTP_RESPONSE" == *"$EXPECTED_OUTPUT"* ]]; then
    echo -e "\n✅ SUCCESS: Integration test passed!"
    echo "Received Body: $HTTP_RESPONSE"
else
    echo -e "\n❌ FAILURE: Response mismatch."
    echo "Expected Body snippet: '$EXPECTED_OUTPUT'"
    echo "Received Body: '$HTTP_RESPONSE'"
    exit 1
fi
