# Quick Start Guide

## 1. Build

```bash
cd DNS_Load_Balancer
./build_and_run.sh
```

## 2. Run

```bash
# Start with round-robin policy (default)
./build/aiori-dnsdist

# Or specify a different policy
./build/aiori-dnsdist leastOutstanding
```

## 3. Test (in another terminal)

```bash
# Single query
dig @127.0.0.1 -p 5353 example.com

# Multiple queries to see load balancing
./test_load_balancing.sh 20
```

## 4. Stop

Press `Ctrl+C` to stop the server and see statistics.

## Available Policies

- `roundrobin` - Default, even distribution
- `leastOutstanding` - Route to least loaded backend
- `wrandom` - Weighted random
- `whashed` - Weighted consistent hashing
- `chashed` - Consistent hashing
- `firstAvailable` - Active/passive failover

## Configuration

Create `build/config.json`:

```json
{
  "backend_pools": [
    {
      "name": "my-pool",
      "servers": ["192.168.1.100", "192.168.1.101", "192.168.1.102"],
      "health_endpoint": "http://192.168.1.100/health",
      "geo_region": "us-east",
      "check_interval_sec": 10
    }
  ]
}
```

## That's it!

See `README_DNSDIST.md` for detailed documentation.
