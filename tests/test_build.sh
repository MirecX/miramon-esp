#!/bin/bash
# Test: Project builds successfully
set -e

cd "$(dirname "$0")/.."

echo "=== Test: Project Skeleton Build ==="

# Test 1: CMakeLists.txt exists
if [ ! -f "CMakeLists.txt" ]; then
    echo "FAIL: CMakeLists.txt missing"
    exit 1
fi
echo "PASS: CMakeLists.txt exists"

# Test 2: main/main.c exists
if [ ! -f "main/main.c" ]; then
    echo "FAIL: main/main.c missing"
    exit 1
fi
echo "PASS: main/main.c exists"

# Test 3: sdkconfig.defaults exists
if [ ! -f "sdkconfig.defaults" ]; then
    echo "FAIL: sdkconfig.defaults missing"
    exit 1
fi
echo "PASS: sdkconfig.defaults exists"

# Test 4: partitions.csv exists
if [ ! -f "partitions.csv" ]; then
    echo "FAIL: partitions.csv missing"
    exit 1
fi
echo "PASS: partitions.csv exists"

# Test 5: Build succeeds
echo "Running: idf.py build..."
source /workspace/idf/export.sh > /dev/null 2>&1
idf.py build > /tmp/build.log 2>&1

if [ $? -eq 0 ]; then
    echo "PASS: Build succeeded"
else
    echo "FAIL: Build failed"
    tail -50 /tmp/build.log
    exit 1
fi

echo "=== All tests passed ==="
