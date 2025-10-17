#!/bin/bash

# Test script for DNS Load Balancer
# This script sends multiple DNS queries to test load balancing

DNS_SERVER="127.0.0.1"
DNS_PORT="5353"
DOMAIN="example.com"
NUM_QUERIES=${1:-20}

echo "🧪 Testing DNS Load Balancer"
echo "   Server: ${DNS_SERVER}:${DNS_PORT}"
echo "   Domain: ${DOMAIN}"
echo "   Queries: ${NUM_QUERIES}"
echo ""

# Check if dig is installed
if ! command -v dig &> /dev/null; then
    echo "❌ 'dig' command not found. Please install dnsutils:"
    echo "   sudo apt install dnsutils"
    exit 1
fi

echo "Sending ${NUM_QUERIES} queries..."
echo ""

# Send multiple queries and collect results
declare -A ip_counts

for i in $(seq 1 $NUM_QUERIES); do
    RESULT=$(dig @${DNS_SERVER} -p ${DNS_PORT} ${DOMAIN} +short 2>/dev/null | head -n1)
    
    if [ -n "$RESULT" ]; then
        echo "Query $i: $RESULT"
        ip_counts["$RESULT"]=$((${ip_counts["$RESULT"]} + 1))
    else
        echo "Query $i: ❌ No response"
    fi
    
    # Small delay to avoid overwhelming the server
    sleep 0.1
done

echo ""
echo "📊 Load Distribution:"
echo ""

total=0
for ip in "${!ip_counts[@]}"; do
    count=${ip_counts[$ip]}
    total=$((total + count))
done

for ip in "${!ip_counts[@]}"; do
    count=${ip_counts[$ip]}
    percentage=$(awk "BEGIN {printf \"%.1f\", ($count / $total) * 100}")
    bar=$(printf '█%.0s' $(seq 1 $((count / 2))))
    printf "  %-15s : %3d queries (%5.1f%%) %s\n" "$ip" "$count" "$percentage" "$bar"
done

echo ""
echo "Total successful queries: $total / $NUM_QUERIES"
