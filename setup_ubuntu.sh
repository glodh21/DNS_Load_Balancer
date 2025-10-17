#!/bin/bash
# Complete Setup Script for Ubuntu
# Run this script to install dependencies, build, and run the DNS Load Balancer

set -e  # Exit on error

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║  DNS Load Balancer with dnsdist - Ubuntu Setup Script         ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# Step 1: Install Dependencies
echo "Step 1/5: Installing dependencies..."
echo "─────────────────────────────────────────────────────────"

if ! command -v cmake &> /dev/null; then
    echo "Installing build tools..."
    sudo apt update
    sudo apt install -y build-essential cmake git pkg-config
else
    echo "✓ Build tools already installed"
fi

if ! pkg-config --exists ldns; then
    echo "Installing LDNS..."
    sudo apt install -y libldns-dev
else
    echo "✓ LDNS already installed"
fi

if ! pkg-config --exists libcurl; then
    echo "Installing libcurl..."
    sudo apt install -y libcurl4-openssl-dev
else
    echo "✓ libcurl already installed"
fi

if [ ! -d "/usr/include/boost" ]; then
    echo "Installing Boost..."
    sudo apt install -y libboost-all-dev
else
    echo "✓ Boost already installed"
fi

if ! command -v dig &> /dev/null; then
    echo "Installing DNS testing tools..."
    sudo apt install -y dnsutils
else
    echo "✓ DNS tools already installed"
fi

echo ""
echo "✅ All dependencies installed!"
echo ""

# Step 2: Build the project
echo "Step 2/5: Building the project..."
echo "─────────────────────────────────────────────────────────"

if [ ! -d "build" ]; then
    mkdir build
fi

cd build
rm -f aiori-dnsdist  # Remove old binary if exists

echo "Running CMake..."
cmake .. || {
    echo "❌ CMake failed. Check error messages above."
    exit 1
}

echo "Compiling..."
make -j$(nproc) || {
    echo "❌ Compilation failed. Check error messages above."
    exit 1
}

echo ""
echo "✅ Build successful!"
echo ""

# Step 3: Create configuration
echo "Step 3/5: Creating configuration..."
echo "─────────────────────────────────────────────────────────"

if [ ! -f "config.json" ]; then
    cat > config.json << 'EOF'
{
  "backend_pools": [
    {
      "name": "test-pool",
      "servers": [
        "8.8.8.8",
        "8.8.4.4",
        "1.1.1.1"
      ],
      "health_endpoint": "http://8.8.8.8",
      "geo_region": "global",
      "check_interval_sec": 10
    }
  ]
}
EOF
    echo "✅ Created config.json with test backends"
else
    echo "✓ config.json already exists"
fi

echo ""

# Step 4: Make scripts executable
echo "Step 4/5: Setting up scripts..."
echo "─────────────────────────────────────────────────────────"

cd ..
chmod +x build_and_run.sh 2>/dev/null || true
chmod +x test_load_balancing.sh 2>/dev/null || true

echo "✅ Scripts ready!"
echo ""

# Step 5: Show next steps
echo "Step 5/5: Setup complete!"
echo "─────────────────────────────────────────────────────────"
echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                    🎉 SETUP COMPLETE! 🎉                       ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""
echo "Next steps:"
echo ""
echo "1️⃣  Run the DNS Load Balancer:"
echo "   cd build"
echo "   ./aiori-dnsdist"
echo ""
echo "2️⃣  In another terminal, test it:"
echo "   dig @127.0.0.1 -p 5353 example.com"
echo ""
echo "3️⃣  Or run the test script:"
echo "   ./test_load_balancing.sh 20"
echo ""
echo "4️⃣  Try different policies:"
echo "   ./build/aiori-dnsdist leastOutstanding"
echo "   ./build/aiori-dnsdist chashed"
echo ""
echo "Available policies:"
echo "  • roundrobin (default)"
echo "  • leastOutstanding"
echo "  • wrandom"
echo "  • whashed"
echo "  • chashed"
echo "  • firstAvailable"
echo ""
echo "📚 Documentation:"
echo "  • BUILD.md - Build instructions"
echo "  • QUICKSTART.md - Quick reference"
echo "  • README_DNSDIST.md - Full documentation"
echo ""
echo "Would you like to run the server now? (y/n)"
read -r response

if [[ "$response" =~ ^[Yy]$ ]]; then
    echo ""
    echo "Starting DNS Load Balancer..."
    echo "Press Ctrl+C to stop"
    echo ""
    cd build
    ./aiori-dnsdist
fi
