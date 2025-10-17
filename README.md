# DNS Load Balancer with Round-Robin

A high-performance DNS load balancer with health checking and round-robin distribution.

## 🎯 Recommended Approach: Use dnsdist

**dnsdist** is PowerDNS's production-ready DNS load balancer. It's the easiest and most powerful option:

```bash
# One-command setup
chmod +x setup-dnsdist.sh
./setup-dnsdist.sh

# Test it
dig @localhost -p 5300 google.com
```

See [DNSDIST_GUIDE.md](DNSDIST_GUIDE.md) for complete guide.

## Features

✅ **Round-Robin Load Balancing** - Fair distribution across backend servers  
✅ **Health Checking** - Automatic exclusion of unhealthy backends  
✅ **Three Operation Modes**:
   - **dnsdist Mode** ⭐ - Production-ready PowerDNS load balancer (RECOMMENDED)
   - **Standalone Mode** - Built-in DNS server (port 5353)
   - **PowerDNS Backend Mode** - Custom PowerDNS pipe backend (port 5300)  
✅ **Dynamic Configuration** - Load servers from config.json  
✅ **Thread-Safe** - Handles concurrent queries  

## Quick Start

### Option 1: dnsdist (Recommended) ⭐

Install and configure dnsdist DNS load balancer:

```bash
# Install and setup
chmod +x setup-dnsdist.sh
./setup-dnsdist.sh
```

Test:
```bash
dig @localhost -p 5300 google.com

# Test round-robin
for i in {1..6}; do dig @localhost -p 5300 google.com +short; done
```

Monitor:
```bash
sudo dnsdist -c
> showServers()
```

Web Dashboard: http://localhost:8083 (password: dnsdist123)

### Option 2: Build Custom Solutions

### Option 2: Build Custom Solutions

If you want to build the custom implementations:

#### Build

```bash
cd build
cmake ..
make
```

This creates two executables:
- `aiori` - Standalone DNS server
- `pdns-backend` - PowerDNS pipe backend

#### Run Standalone Mode

```bash
cd build
./aiori
```

Test:
```bash
dig @127.0.0.1 -p 5353 example.com
```

#### Run with PowerDNS Backend

See [POWERDNS_INTEGRATION.md](POWERDNS_INTEGRATION.md) for complete setup guide.

Quick test:
```bash
cd build
./pdns-backend config.json
```

## Comparison of Modes

| Feature | dnsdist ⭐ | Standalone | PowerDNS Backend |
|---------|-----------|------------|------------------|
| **Setup** | 1 command | Build required | Build + PowerDNS |
| **Performance** | Millions qps | Medium | High |
| **Production Ready** | ✅ Yes | ⚠️ Testing | ✅ Yes |
| **Maintenance** | Official support | Custom code | Custom code |
| **Features** | Advanced | Basic | Medium |
| **Monitoring** | Web UI + Console | Logs only | PowerDNS tools |
| **Health Checks** | Built-in | Custom | Custom |
| **Use Case** | **Production** | Development | Custom integration |

**Recommendation**: Use **dnsdist** for production deployments!

## Configuration

### dnsdist Configuration

Edit `dnsdist.conf` to configure backend DNS servers:

```lua
-- Add backend servers
newServer({
    address="8.8.8.8:53",
    name="google-dns-1",
    weight=3,
    checkInterval=5
})

-- Set load balancing policy
setServerPolicy(roundrobin)
```

### Custom Solutions Configuration

Edit `build/config.json` to configure backend servers:

```json
{
  "pools": [
    {
      "name": "production",
      "servers": [
        {"ip": "8.8.8.8", "port": 53, "weight": 3},
        {"ip": "8.8.4.4", "port": 53, "weight": 3},
        {"ip": "1.1.1.1", "port": 53, "weight": 2}
      ],
      "health_endpoint": "http://127.0.0.1:8080/health",
      "geo_region": "us-east",
      "check_interval_sec": 10
    }
  ]
}
```

## Testing Round-Robin

Run multiple queries to see load balancing:

```bash
for i in {1..6}; do dig @localhost -p 5353 example.com +short; done
```

Expected output (rotating IPs):
```
8.8.8.8
8.8.4.4
1.1.1.1
8.8.8.8
8.8.4.4
1.1.1.1
```

## Documentation

- [DNSDIST_GUIDE.md](DNSDIST_GUIDE.md) ⭐ - **dnsdist setup guide (RECOMMENDED)**
- [BUILD.md](BUILD.md) - Build instructions for custom solutions
- [ARCHITECTURE.md](ARCHITECTURE.md) - System architecture and design
- [POWERDNS_INTEGRATION.md](POWERDNS_INTEGRATION.md) - PowerDNS backend setup
- [QUICKREF.md](QUICKREF.md) - Quick reference commands

## Project Structure

```
DNS_Load_Balancer/
├── dnsdist.conf                       # dnsdist configuration ⭐
├── setup-dnsdist.sh                   # dnsdist setup script ⭐
├── src/
│   ├── config/
│   │   ├── config_loader.cpp/h       # Config file parsing
│   │   ├── health_checker.cpp/h      # Backend health monitoring
│   │   ├── load_balancer.cpp/h       # Round-robin algorithm
│   │   └── powerdns_backend.cpp/h    # PowerDNS integration
│   └── main/
│       ├── dns-idk.cpp                # Standalone DNS server
│       └── powerdns_main.cpp          # PowerDNS backend entry
├── build/
│   ├── aiori                          # Standalone executable
│   ├── pdns-backend                   # PowerDNS backend
│   └── config.json                    # Runtime configuration
├── DNSDIST_GUIDE.md                   # dnsdist documentation ⭐
├── pdns.conf                          # PowerDNS configuration
└── CMakeLists.txt                     # Build configuration
```

## How It Works

### dnsdist Mode (Recommended)

1. **Client sends DNS query** for `google.com`
2. **dnsdist** receives query on port 5300
3. **Load Balancer** (dnsdist built-in) selects backend using round-robin
4. **Health Checker** (dnsdist built-in) ensures only healthy backends
5. **Query forwarded** to selected backend (8.8.8.8, 8.8.4.4, or 1.1.1.1)
6. **Backend responds** with DNS answer
7. **dnsdist forwards** response back to client
8. **Next query** gets different backend (round-robin rotation)

### Custom Solutions Mode

1. **Client sends DNS query** for `example.com`
2. **DNS Server** (standalone or PowerDNS) receives query
3. **Load Balancer** gets list of healthy servers from Health Checker
4. **Round-Robin selection** picks next server in rotation
5. **DNS response** sent back with selected backend IP
6. **Next query** gets different IP (round-robin rotation)

## Requirements

### For dnsdist (Recommended)
- Ubuntu/Debian or Fedora/RHEL
- dnsdist package (installed by setup script)
- dig (for testing)

### For Custom Solutions
- CMake 3.10+
- C++17 compiler
- libldns (for standalone mode)
- Boost.Asio (for standalone mode)
- libcurl (for health checking)
- nlohmann-json (for config parsing)
- PowerDNS (optional, for PowerDNS backend mode)

## License

See LICENSE file for details.

