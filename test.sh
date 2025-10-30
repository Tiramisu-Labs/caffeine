#!/bin/bash
# ----------------------------------------------------------------------
# CAFFEINE INTEGRATION TEST SUITE
# This script compiles a test handler, starts a Caffeine instance,
# sends a request, verifies the response, and cleans up all files.
# ----------------------------------------------------------------------

# --- Configuration ---
CAFFEINE_EXE="./caffeine"
HANDLER_NAME="echo_handler"
TEST_INSTANCE_NAME="integration_test"
TEST_PORT="8080"
TEST_URL="http://127.0.0.1:${TEST_PORT}/${HANDLER_NAME}"
EXPECTED_OUTPUT="Caffeine handler executed successfully! Instance: integration_test"

# --- Dynamic Path Setup (Matches C code's defaults) ---
CAFFEINE_HOME="${HOME}/.config/caffeine"
PID_FILE="/tmp/caffeine_${TEST_INSTANCE_NAME}.pid"
LOG_FILE="${HOME}/.local/share/caffeine/${TEST_INSTANCE_NAME}.log"

# --- Functions ---

# Function to stop the server and clean up files
cleanup() {
    echo -e "\n--- CLEANUP ---"
    
    # 1. Stop the running instance gracefully
    if [ -f "$PID_FILE" ]; then
        echo "Stopping Caffeine instance ($TEST_INSTANCE_NAME)..."
        "$CAFFEINE_EXE" -s --name "$TEST_INSTANCE_NAME"
        # Give it a moment to shut down
        sleep 1
    fi
    
    # 2. Clean up files
    rm -f "$PID_FILE"
    rm -f "./${HANDLER_NAME}"
    rm -f "$CAFFEINE_HOME/${HANDLER_NAME}"
    
    # 3. Clean up log file (optional, but good for testing)
    # rm -f "$LOG_FILE"
    
    echo "Cleanup complete."
}

# Ensure cleanup runs on exit or script failure
trap cleanup EXIT

# --- Execution ---

echo "--- 1. PREPARING TEST ENVIRONMENT ---"

# Check if the Caffeine executable exists
if [ ! -x "$CAFFEINE_EXE" ]; then
    echo "ERROR: Caffeine executable not found at '$CAFFEINE_EXE'. Please compile and ensure it's in the current directory."
    exit 1
fi

# Create handler execution directory if it doesn't exist
mkdir -p "$CAFFEINE_HOME"
echo "Target handler directory: $CAFFEINE_HOME"

# Check for a pre-existing running instance and stop it
if [ -f "$PID_FILE" ]; then
    echo "WARNING: Found existing PID file. Stopping old instance..."
    "$CAFFEINE_EXE" -s --name "$TEST_INSTANCE_NAME"
    sleep 1
fi

echo "--- 2. COMPILING AND DEPLOYING HANDLER ---"

# Compile the test handler
gcc test_handler.c -o $HANDLER_NAME 
if [ $? -ne 0 ]; then
    echo "ERROR: Handler compilation failed."
    exit 1
fi

# Move the compiled handler to the execution path
mv $HANDLER_NAME "$CAFFEINE_HOME/"
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to move handler to $CAFFEINE_HOME. Check permissions."
    exit 1
fi
echo "Handler deployed to: $CAFFEINE_HOME/$HANDLER_NAME"

echo "--- 3. STARTING CAFFEINE INSTANCE ---"

# Start the server as a daemon on the test port
# Note: Since the core logic checks 'bind()' before daemonizing, 
# any startup errors will show on the console here.
"$CAFFEINE_EXE" -d -n "$TEST_INSTANCE_NAME" -p "$TEST_PORT" -w 1 
if [ $? -ne 0 ]; then
    echo "FATAL ERROR: Caffeine failed to start."
    exit 1
fi
echo "Caffeine started as daemon. PID file: $PID_FILE"

# Wait for the server to fully initialize
sleep 2

echo "--- 4. EXECUTING HTTP TEST REQUEST ---"

# Make the HTTP request using curl and store the body
echo "Requesting: $TEST_URL"
HTTP_RESPONSE=$(curl -s --max-time 5 "$TEST_URL")
CURL_STATUS=$?

if [ $CURL_STATUS -ne 0 ]; then
    echo "ERROR: Curl failed to connect or timed out (Status: $CURL_STATUS)."
    echo "       Check log file: $LOG_FILE"
    exit 1
fi

echo "--- 5. VALIDATING RESPONSE ---"

# Check if the response matches the expected output
if [[ "$HTTP_RESPONSE" == *"$EXPECTED_OUTPUT"* ]]; then
    echo -e "\n✅ SUCCESS: Integration test passed!"
    echo "Received Body: $HTTP_RESPONSE"
else
    echo -e "\n❌ FAILURE: Response mismatch."
    echo "Expected Body snippet: '$EXPECTED_OUTPUT'"
    echo "Received Body: '$HTTP_RESPONSE'"
    exit 1
fi

# The 'trap cleanup EXIT' command will handle the cleanup now.
