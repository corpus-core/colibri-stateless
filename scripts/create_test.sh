#!/bin/bash

# Script to create a proof and verify it
# Usage: ./create_test.sh <arguments>

echo "Running proof with arguments: $@"
../build/bin/proof -o proof.ssz -t "$@"

# Check if proof command was successful
if [ $? -ne 0 ]; then
    echo "Error: Proof generation failed!"
    exit 1
fi

echo "Running verify with arguments: $@"
../build/bin/verify -i proof.ssz -t "$@"

# Check if verify command was successful
if [ $? -ne 0 ]; then
    echo "Error: Verification failed!"
    exit 1
fi

echo "Test completed successfully!"
