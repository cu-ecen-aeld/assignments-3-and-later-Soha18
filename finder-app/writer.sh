#!/bin/bash

# Check if two arguments are provided
if [ "$#" -ne 2 ]; then
    echo "Error: Missing arguments."
    echo "Usage: $0 <file path> <string to write>"
    exit 1
fi

# Assign arguments to variables
writefile=$1
writestr=$2

# Create directories for the file if they don't exist
mkdir -p "$(dirname "$writefile")"

# Write the string to the file
echo "$writestr" > "$writefile" 2>/dev/null

# Check if the file was created successfully
if [ $? -ne 0 ]; then
    echo "Error: Could not create the file '$writefile'."
    exit 1
fi

# Success message (optional)
echo "File '$writefile' created successfully with the content: $writestr"

