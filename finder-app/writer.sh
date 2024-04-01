#!/bin/bash

if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Not all required arguments were provided."
    exit 1
fi

writefile=$1
writestr=$2

if ! echo $writestr >$writefile; then
    echo "Could not write to \"${writefile}\"."
    exit 1
fi
