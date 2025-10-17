/*
 * Standalone DNS Load Balancer Core - Extracted from PowerDNS dnsdist
 * Architecture preserved, external dependencies minimized
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <optional>
#include <unordered_map>
#include <algorithm>
#include <limits>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <mutex>
#include <shared_mutex>

namespace dnsdist_lb {

// ============================================================================
// Core Data Structures (Minimal dependencies)
// ============================================================================

// Simple hash calculation for DNS names
struct DNSNameHash {
    size_t hash;
    DNSNameHash(const std::string& name, uint32_t perturbation = 0);
};

// Query metadata (minimal)
struct QueryContext {
    DNSNameHash qname;
    uint16_t qtype;
    uint16_t qclass;
    std::unordered_map<std::string, std::string> tags;
};

// Backend server state
struct BackendServer {
    struct Config {
        std::string name;
        std::string address;
        int port{53};
        int order{1};
        int weight{1};
        bool tcpOnly{false};
    };

    Config config;
    std::atomic<uint64_t> outstanding{0};
    std::atomic<uint64_t> queries{0};
    std::atomic<uint64_t> responses{0};
    std::atomic<double> latencyUsec{0.0};
    std::atomic<double> latencyUsecTCP{0.0};
    std::atomic<bool> isUp{true};
    
    // For consistent hashing
    std::vector<uint32_t> hashes;
    std::atomic<bool> hashesComputed{false};
    mutable std::shared_mutex hashesMutex;

    BackendServer(const Config& cfg) : config(cfg) {}

    bool isAvailable() const { return isUp.load(std::memory_order_relaxed); }
    
    double getRelevantLatencyUsec() const {
        return config.tcpOnly ? latencyUsecTCP.load() : latencyUsec.load();
    }

    void computeHashes();
};

// ============================================================================
// Load Balancing Policies (Core algorithms from dnsdist)
// ============================================================================

class LoadBalancingPolicy {
public:
    using ServerVector = std::vector<std::shared_ptr<BackendServer>>;
    using PolicyFunc = std::function<std::optional<size_t>(const ServerVector&, const QueryContext*)>;

    LoadBalancingPolicy(const std::string& name, PolicyFunc func)
        : name_(name), policy_(std::move(func)) {}

    std::shared_ptr<BackendServer> selectServer(const ServerVector& servers, const QueryContext* ctx) const {
        if (servers.empty()) {
            return nullptr;
        }
        
        auto index = policy_(servers, ctx);
        if (!index || *index >= servers.size()) {
            return nullptr;
        }
        
        return servers[*index];
    }

    const std::string& getName() const { return name_; }

private:
    std::string name_;
    PolicyFunc policy_;
};

// ============================================================================
// Policy Implementations (Exact dnsdist algorithms)
// ============================================================================

// Configuration for weighted policies
struct LoadBalancingConfig {
    double weightedBalancingFactor{0.0};
    double consistentHashBalancingFactor{0.0};
    uint32_t hashPerturbation{0};
    bool roundrobinFailOnNoServer{false};
};

// Get global configuration
LoadBalancingConfig& getGlobalLBConfig();

// Round-robin policy
std::optional<size_t> roundrobin(
    const std::vector<std::shared_ptr<BackendServer>>& servers,
    const QueryContext* ctx);

// Least outstanding queries policy
std::optional<size_t> leastOutstanding(
    const std::vector<std::shared_ptr<BackendServer>>& servers,
    const QueryContext* ctx);

// First available server policy
std::optional<size_t> firstAvailable(
    const std::vector<std::shared_ptr<BackendServer>>& servers,
    const QueryContext* ctx);

// Weighted random policy
std::optional<size_t> wrandom(
    const std::vector<std::shared_ptr<BackendServer>>& servers,
    const QueryContext* ctx);

// Weighted hashed policy
std::optional<size_t> whashed(
    const std::vector<std::shared_ptr<BackendServer>>& servers,
    const QueryContext* ctx);

// Consistent hashed policy
std::optional<size_t> chashed(
    const std::vector<std::shared_ptr<BackendServer>>& servers,
    const QueryContext* ctx);

// Ordered weighted random untag policy
std::optional<size_t> orderedWrandUntag(
    const std::vector<std::shared_ptr<BackendServer>>& servers,
    const QueryContext* ctx);

// ============================================================================
// Server Pool Management
// ============================================================================

class ServerPool {
public:
    ServerPool() : policy_(std::make_shared<LoadBalancingPolicy>("roundrobin", roundrobin)) {}

    void addServer(std::shared_ptr<BackendServer> server);
    void removeServer(std::shared_ptr<BackendServer> server);
    void setPolicy(std::shared_ptr<LoadBalancingPolicy> policy);
    
    std::shared_ptr<BackendServer> selectServer(const QueryContext* ctx) const;
    const std::vector<std::shared_ptr<BackendServer>>& getServers() const;

private:
    mutable std::shared_mutex mutex_;
    std::vector<std::shared_ptr<BackendServer>> servers_;
    std::shared_ptr<LoadBalancingPolicy> policy_;
};

// ============================================================================
// Main Load Balancer Interface
// ============================================================================

class LoadBalancer {
public:
    LoadBalancer();

    // Add backend server
    void addBackend(const BackendServer::Config& config, const std::string& poolName = "");
    
    // Remove backend server by name
    void removeBackend(const std::string& name, const std::string& poolName = "");
    
    // Set policy for a pool
    void setPoolPolicy(const std::string& poolName, const std::string& policyName);
    
    // Select server for query
    std::shared_ptr<BackendServer> selectServer(
        const std::string& qname,
        uint16_t qtype,
        uint16_t qclass,
        const std::string& poolName = "");

    // Get all backends from a pool
    std::vector<std::shared_ptr<BackendServer>> getBackends(const std::string& poolName = "") const;
    
    // Get available policies
    static std::vector<std::string> getAvailablePolicies();

    // Configuration
    void setWeightedBalancingFactor(double factor);
    void setConsistentHashBalancingFactor(double factor);
    void setHashPerturbation(uint32_t perturbation);

private:
    std::unordered_map<std::string, ServerPool> pools_;
    mutable std::shared_mutex poolsMutex_;
    
    std::shared_ptr<LoadBalancingPolicy> getPolicyByName(const std::string& name);
    ServerPool& getOrCreatePool(const std::string& name);
};

} // namespace dnsdist_lb
