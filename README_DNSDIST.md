# DNS Load Balancer with PowerDNS/dnsdist Algorithms

This project integrates the powerful load balancing algorithms from PowerDNS/dnsdist with a Boost.Asio-based DNS server.

## Features

- **Multiple Load Balancing Policies**: Supports all major dnsdist load balancing algorithms
  - `roundrobin`: Distribute queries evenly across backends
  - `leastOutstanding`: Send to backend with fewest pending queries
  - `wrandom`: Weighted random selection
  - `whashed`: Weighted consistent hashing
  - `chashed`: Consistent hashing
  - `firstAvailable`: Always use first available backend

- **Health Checking**: Continuous health monitoring of backend servers
- **High Performance**: Multi-threaded DNS server using Boost.Asio
- **Production-Grade Logic**: Uses the same algorithms as PowerDNS dnsdist

## Building

### Prerequisites

```bash
sudo apt install build-essential cmake libboost-all-dev libldns-dev libcurl4-openssl-dev
```

### Compile

```bash
mkdir -p build
cd build
cmake ..
make
```

This will create three executables:
- `aiori` - Original DNS load balancer with basic algorithms
- `pdns-backend` - PowerDNS pipe backend integration
- `aiori-dnsdist` - DNS load balancer with dnsdist algorithms (NEW!)

## Usage

### Running with dnsdist Algorithms

```bash
# Run with default round-robin policy
./build/aiori-dnsdist

# Run with specific load balancing policy
./build/aiori-dnsdist roundrobin
./build/aiori-dnsdist leastOutstanding
./build/aiori-dnsdist wrandom
./build/aiori-dnsdist whashed
./build/aiori-dnsdist chashed
./build/aiori-dnsdist firstAvailable
```

### Configuration

Create a `config.json` file in the build directory or parent directory:

```json
{
  "backend_pools": [
    {
      "name": "primary-pool",
      "servers": [
        "192.168.1.100",
        "192.168.1.101",
        "192.168.1.102"
      ],
      "health_endpoint": "http://192.168.1.100/health",
      "geo_region": "us-east",
      "check_interval_sec": 10
    },
    {
      "name": "secondary-pool",
      "servers": [
        "192.168.2.100",
        "192.168.2.101"
      ],
      "health_endpoint": "http://192.168.2.100/health",
      "geo_region": "us-west",
      "check_interval_sec": 15
    }
  ]
}
```

### Testing

```bash
# Test DNS query (from another terminal)
dig @127.0.0.1 -p 5353 example.com

# Run multiple queries to see load balancing in action
for i in {1..10}; do dig @127.0.0.1 -p 5353 example.com +short; done
```

## Load Balancing Policies Explained

### Round Robin (`roundrobin`)
Distributes queries evenly across all healthy backends in a circular manner. Simple and effective for most use cases.

**Best for**: General purpose load distribution, stateless applications

### Least Outstanding (`leastOutstanding`)
Sends queries to the backend with the fewest pending queries. Helps balance load when backends have different response times.

**Best for**: Backends with varying response times, avoiding overload

### Weighted Random (`wrandom`)
Randomly selects backends with weights. Backends with higher weights receive more queries.

**Best for**: Gradual traffic shifting, A/B testing

### Weighted Hashed (`whashed`)
Uses consistent hashing with weights. Same client IP gets routed to same backend (when healthy).

**Best for**: Session persistence, cache locality

### Consistent Hashed (`chashed`)
Pure consistent hashing. Minimal disruption when backends are added/removed.

**Best for**: Distributed caching, stateful applications

### First Available (`firstAvailable`)
Always uses the first healthy backend in the list. Others act as failover.

**Best for**: Active/passive failover scenarios

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    DNS Client Query                          │
└──────────────────────────┬──────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│              Boost.Asio DNS Server (DnsServer)              │
│              - UDP socket on port 5353                       │
│              - Multi-threaded request handling               │
└──────────────────────────┬──────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│         DnsdistLoadBalancer (Load Balancing Logic)          │
│         - Policy-based server selection                      │
│         - Health-aware routing                               │
│         - Statistics tracking                                │
└──────────────────────────┬──────────────────────────────────┘
                           │
                    ┌──────┴──────┐
                    │             │
                    ▼             ▼
         ┌───────────────┐  ┌──────────────┐
         │ Health Checker│  │ dnsdist LB   │
         │  - HTTP checks│  │  Policies    │
         │  - Monitoring │  │  - Core      │
         └───────┬───────┘  │    Algorithms│
                 │          └──────────────┘
                 │
    ┌────────────┼────────────┐
    │            │            │
    ▼            ▼            ▼
┌────────┐  ┌────────┐  ┌────────┐
│Backend │  │Backend │  │Backend │
│  #1    │  │  #2    │  │  #3    │
└────────┘  └────────┘  └────────┘
```

## File Structure

```
DNS_Load_Balancer/
├── src/
│   ├── main/
│   │   ├── dns-idk.cpp           # Original DNS server
│   │   ├── main_dnsdist_lb.cpp   # NEW: dnsdist-integrated main
│   │   └── powerdns_main.cpp     # PowerDNS backend
│   └── config/
│       ├── config_loader.h/cpp   # Configuration management
│       ├── health_checker.h/cpp  # Health checking logic
│       └── load_balancer.h/cpp   # Basic load balancer
├── load_balancing/
│   ├── dnsdist-lbpolicies.hh/cc  # Load balancing algorithms
│   ├── dnsdist-backend.hh        # Backend management
│   └── dnsdist.hh                # Core dnsdist types
├── CMakeLists.txt                # Build configuration
└── README_DNSDIST.md             # This file
```

## Performance Tips

1. **Choose the right policy**: Different policies have different performance characteristics
2. **Tune thread count**: Adjust the number of worker threads based on CPU cores
3. **Health check interval**: Balance between responsiveness and overhead
4. **Backend pool size**: More backends = better distribution but more health checks

## Troubleshooting

### No healthy backends
```
❌ No healthy backends available
```
- Check that backend servers are running
- Verify health check endpoints are accessible
- Review health checker logs

### Policy not working as expected
```
⚠️  Unknown policy 'xyz', using roundrobin
```
- Verify policy name spelling
- Use one of the supported policies listed above

### Build errors
```
fatal error: dnsdist-lbpolicies.hh: No such file or directory
```
- Ensure all files are present in load_balancing/ directory
- Check include paths in CMakeLists.txt

## Monitoring

The load balancer prints statistics when you stop it (Ctrl+C):

```
📊 Load Balancer Statistics:
   Policy: roundrobin
   Total Backends: 3
   Healthy Backends: 2
   Backend 0: 192.168.1.100 ✓ (342 queries)
   Backend 1: 192.168.1.101 ✓ (338 queries)
   Backend 2: 192.168.1.102 ✗ (0 queries)
```

## Contributing

The core load balancing logic comes from PowerDNS/dnsdist. When updating:
1. Keep the complex algorithms unchanged
2. Maintain compatibility with the DNS server architecture
3. Update this documentation

## License

This integrates code from PowerDNS/dnsdist (GPL v2). See individual files for copyright notices.
