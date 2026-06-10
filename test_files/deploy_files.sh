#!/bin/bash

HANDLER_FILES=("static_response.c" "dynamic_buffer.c" "dynamic_no_length.c")
SO_FILES=()
SUCCESS_COUNT=0
FAILURE_COUNT=0

compile_handler() {
    local C_FILE=$1
    local SO_FILE="${C_FILE%.c}.so"
    
    echo "Compiling $C_FILE -> $SO_FILE..."
    
    if gcc -shared -fPIC "$C_FILE" -o "$SO_FILE"; then
        echo "Compilation successful."
        SO_FILES+=("$SO_FILE")
        return 0
    else
        echo "Compilation failed for $C_FILE."
        return 1
    fi
}

deploy_handler() {
    local C_FILE=$1
    local SO_FILE="${C_FILE%.c}.so"
    
    echo "Deploying $SO_FILE..."
    
    if caffeine --deploy "$SO_FILE"; then
        echo "Deployment successful. Handler ready at /${SO_FILE%.so}"
        return 0
    else
        echo "Deployment failed for $SO_FILE. Is caffeine server running?"
        return 1
    fi
}

cleanup_handlers() {
    echo "--- Starting Cleanup ---"
    if [ ${#SO_FILES[@]} -eq 0 ]; then
        echo "No .so files were successfully compiled to clean up."
        return 0
    fi
    
    for so in "${SO_FILES[@]}"; do
        if [ -f "$so" ]; then
            rm -f "$so"
            echo "Removed $so."
        else
            echo "Warning: $so not found in current directory."
        fi
    done
    echo "Cleanup finished."
}

echo "--- Starting Caffeine Handler Compilation and Deployment ---"

for file in "${HANDLER_FILES[@]}"; do
    if [ -f "$file" ]; then
        if compile_handler "$file"; then
            if deploy_handler "$file"; then
                SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
            else
                FAILURE_COUNT=$((FAILURE_COUNT + 1))
            fi
        else
            FAILURE_COUNT=$((FAILURE_COUNT + 1))
        fi
    else
        echo "Skipping $file: Source file not found."
    fi
    echo "--------------------------------------------------------"
done

echo "--- Summary ---"
echo "Successful Deployments: $SUCCESS_COUNT"
echo "Failed Deployments: $FAILURE_COUNT"

if [ $FAILURE_COUNT -eq 0 ]; then
    echo "Deployment complete. Handlers are ready for testing."
else
    echo "One or more handlers failed to compile or deploy. Check logs."
fi

echo "--- Clean up ---"

cleanup_handlers