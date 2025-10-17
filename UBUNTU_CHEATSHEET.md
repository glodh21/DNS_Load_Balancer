# Ubuntu Quick Commands Cheat Sheet

## 🚀 First Time Setup (One Command)

```bash
chmod +x setup_ubuntu.sh && ./setup_ubuntu.sh
```

This will:
- Install all dependencies
- Build the project
- Create configuration
- Start the server

## 📦 Manual Installation

```bash
# Install dependencies
sudo apt update
sudo apt install -y build-essential cmake libboost-all-dev libldns-dev libcurl4-openssl-dev dnsutils

# Build
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

## ▶️ Running

```bash
# Default (round-robin)
./build/aiori-dnsdist

# With specific policy
./build/aiori-dnsdist leastOutstanding
./build/aiori-dnsdist chashed
```

## 🧪 Testing

```bash
# Single query
dig @127.0.0.1 -p 5353 example.com

# Multiple queries
for i in {1..10}; do dig @127.0.0.1 -p 5353 example.com +short; done

# Or use test script
./test_load_balancing.sh 20
```

## 🛑 Stopping

Press `Ctrl+C` - will show statistics

## 📋 Policies

- `roundrobin` - Even distribution (default)
- `leastOutstanding` - Route to least loaded
- `wrandom` - Weighted random
- `whashed` - Weighted hash
- `chashed` - Consistent hash
- `firstAvailable` - Active/passive failover

## ⚙️ Configuration

Edit `build/config.json`:
```json
{
  "backend_pools": [{
    "name": "my-pool",
    "servers": ["192.168.1.100", "192.168.1.101"],
    "health_endpoint": "http://192.168.1.100/health",
    "check_interval_sec": 10
  }]
}
```

## 🔧 Troubleshooting

```bash
# Port already in use?
sudo lsof -i :5353

# Rebuild from scratch
cd build && rm -rf * && cmake .. && make

# Check dependencies
pkg-config --libs ldns
pkg-config --libs libcurl
```

## 📁 Important Files

- `aiori-dnsdist` - Main executable
- `config.json` - Backend configuration
- `BUILD.md` - Detailed build instructions
- `README_DNSDIST.md` - Full documentation

## 💡 Quick Test Sequence

```bash
# Terminal 1
cd DNS_Load_Balancer/build
./aiori-dnsdist

# Terminal 2
dig @127.0.0.1 -p 5353 example.com

# See it work!
```
