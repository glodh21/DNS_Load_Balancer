# Building and Running on Ubuntu

## Prerequisites

### Install Required Dependencies

```bash
# Update system
sudo apt update
sudo apt upgrade -y

# Install build tools
sudo apt install -y build-essential cmake git pkg-config

# Install libraries
sudo apt install -y libboost-all-dev libldns-dev libcurl4-openssl-dev

# Install testing tools (optional)
sudo apt install -y dnsutils
```

### Verify Installation
```bash
cmake --version      # Should be 3.10+
g++ --version        # Should support C++17
pkg-config --exists ldns && echo "✓ LDNS found"
pkg-config --exists libcurl && echo "✓ libcurl found"
```

## Building the Project

### Quick Build (Recommended)

```bash
# Make script executable
chmod +x build_and_run.sh

# Build the project
./build_and_run.sh
```

### Manual Build Steps

```bash
# 1. Create build directory
mkdir -p build
cd build

# 2. Run CMake
cmake ..

# 3. Compile
make -j$(nproc)

# This creates three executables:
# - aiori (original DNS load balancer)
# - pdns-backend (PowerDNS integration)
# - aiori-dnsdist (DNS load balancer with dnsdist algorithms) ⭐
```

## Configuration

Create `config.json` in the build directory:

```bash
cd build
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
```

## Running the Server

### Start with Default Policy (Round Robin)
```bash
cd build
./aiori-dnsdist
```

### Start with Specific Policy
```bash
# Least outstanding (route to least loaded backend)
./aiori-dnsdist leastOutstanding

# Consistent hashing (same client → same backend)
./aiori-dnsdist chashed

# Weighted random
./aiori-dnsdist wrandom

# First available (active/passive failover)
./aiori-dnsdist firstAvailable
```

### Expected Output
```
🚀 Starting DNS Load Balancer with PowerDNS/dnsdist algorithms...
📂 Current working directory: /path/to/DNS_Load_Balancer/build
✅ Successfully loaded config from: config.json
⚖️  Initializing dnsdist load balancer...
📋 Load balancing policy set to: roundrobin
🌐 Starting DNS server...
✅ DNS server started on port 5353

🎯 DNS Load Balancer is running!
   Policy: roundrobin
   Threads: 4
   Press Ctrl+C to stop.
```

## Testing

### Open a New Terminal

```bash
# Single test query
dig @127.0.0.1 -p 5353 example.com

# Multiple queries to see load balancing
for i in {1..10}; do
  dig @127.0.0.1 -p 5353 example.com +short
done
```

### Use Test Script
```bash
cd ~/DNS_Load_Balancer
chmod +x test_load_balancing.sh
./test_load_balancing.sh 20
```

### Expected Test Output
```
🧪 Testing DNS Load Balancer
Sending 20 queries...

📊 Load Distribution:
  8.8.8.8         :   7 queries ( 35.0%) ███
  8.8.4.4         :   7 queries ( 35.0%) ███
  1.1.1.1         :   6 queries ( 30.0%) ███
```

## Stopping the Server

Press `Ctrl+C` to see statistics:
```
^C Received signal 2, shutting down gracefully...

📊 Load Balancer Statistics:
   Policy: roundrobin
   Total Backends: 3
   Healthy Backends: 3
   Backend 0: 8.8.8.8 ✓ (342 queries)
   Backend 1: 8.8.4.4 ✓ (338 queries)
   Backend 2: 1.1.1.1 ✓ (340 queries)
```

## Troubleshooting

### "Address already in use"
```bash
# Check what's using port 5353
sudo lsof -i :5353

# Kill the process or change port in source code
```

### CMake can't find libraries
```bash
# Reinstall dependencies
sudo apt install --reinstall libldns-dev libboost-all-dev libcurl4-openssl-dev

# Clear CMake cache and rebuild
cd build
rm -rf *
cmake ..
make
```

### "undefined reference" errors

This means the build cache is stale. Solution:
```bash
cd build
rm -rf *
cmake ..
make
```

### No healthy backends
```bash
# Check if backends are reachable
ping 8.8.8.8

# Adjust config.json with servers you control
```

### Missing dependencies

If you get errors about missing libraries, install:

```bash
# Ubuntu/Debian
sudo apt-get install libldns-dev libboost-dev libcurl4-openssl-dev nlohmann-json3-dev

# Fedora/RHEL
sudo dnf install ldns-devel boost-devel libcurl-devel json-devel
```

### Member initialization order warning

This has been fixed. If you still see it, make sure you have the latest version of `dns-idk.cpp`.

## Running the Server

After successful build:

```bash
cd build
./aiori
```

## Testing

Test DNS queries:

```bash
# Single query
dig @localhost -p 5353 example.com

# Multiple queries to see round-robin
for i in {1..5}; do dig @localhost -p 5353 example.com +short; done
```

You should see different IP addresses being returned in round-robin fashion!
