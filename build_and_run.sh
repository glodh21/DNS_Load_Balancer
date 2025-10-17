#!/bin/bash

# Build and Run Script for DNS Load Balancer with dnsdist

set -e  # Exit on error

echo "🔨 Building DNS Load Balancer with dnsdist algorithms..."
echo ""

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir -p build
fi

cd build

# Run CMake
echo "Running CMake..."
cmake ..

# Build
echo "Building..."
make -j$(nproc)

echo ""
echo "✅ Build complete!"
echo ""
echo "Available executables:"
echo "  - aiori           : Original DNS load balancer"
echo "  - pdns-backend    : PowerDNS pipe backend"
echo "  - aiori-dnsdist   : DNS load balancer with dnsdist algorithms"
echo ""

# Check if user wants to run
if [ "$1" == "run" ]; then
    POLICY=${2:-roundrobin}
    echo "🚀 Starting aiori-dnsdist with policy: $POLICY"
    echo ""
    ./aiori-dnsdist $POLICY
else
    echo "To run the DNS load balancer:"
    echo "  ./build/aiori-dnsdist [policy]"
    echo ""
    echo "Available policies:"
    echo "  - roundrobin (default)"
    echo "  - leastOutstanding"
    echo "  - wrandom"
    echo "  - whashed"
    echo "  - chashed"
    echo "  - firstAvailable"
    echo ""
    echo "Example:"
    echo "  ./build/aiori-dnsdist roundrobin"
    echo ""
    echo "Or use this script:"
    echo "  ./build_and_run.sh run [policy]"
fi
