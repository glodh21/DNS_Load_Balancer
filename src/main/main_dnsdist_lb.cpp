/**
 * DNS Load Balancer with PowerDNS/dnsdist Load Balancing Logic
 * 
 * This file integrates the powerful dnsdist load balancing algorithms
 * with the existing Boost.Asio DNS server architecture.
 */

#include <boost/asio.hpp>
#include <ldns/ldns.h>
#include <iostream>
#include <thread>
#include <vector>
#include <memory>
#include <signal.h>
#include <unistd.h>
#include <atomic>
#include <map>

// Load balancing includes from dnsdist
#include "../../load_balancing/dnsdist-lbpolicies.hh"
#include "../../load_balancing/dnsdist-backend.hh"
#include "../../load_balancing/dnsdist.hh"

// Configuration includes
#include "../config/config_loader.h"
#include "../config/health_checker.h"

using namespace std;
using boost::asio::ip::udp;

const int DNS_PORT = 5353; // Use 53 if running as root
const char* ZONE_NAME = "example.com."; // Our zone

/**
 * Wrapper class to integrate dnsdist backend servers with our health checker
 */
class DnsdistLoadBalancer {
public:
    DnsdistLoadBalancer(const std::vector<ServerPool>& pools, HealthChecker* health_checker)
        : health_checker_(health_checker), current_policy_name_("roundrobin") {
        
        if (!health_checker_) {
            throw std::runtime_error("HealthChecker cannot be null");
        }
        
        // Initialize server pools and create DownstreamState objects
        initializeBackends(pools);
        
        // Set default policy to round-robin
        setPolicy("roundrobin");
        
        std::cout << "✅ DnsdistLoadBalancer initialized with " 
                  << backends_.size() << " backend servers" << std::endl;
    }
    
    /**
     * Get the next server IP for a DNS query using the configured load balancing policy
     */
    std::string getServerForQuery(const std::string& domain) {
        // Filter healthy servers only
        ServerPolicy::NumberedServerVector available_servers;
        
        for (size_t i = 0; i < backends_.size(); ++i) {
            auto& backend = backends_[i];
            std::string backend_ip = getBackendIP(backend);
            
            // Check if server is healthy
            if (health_checker_->isHealthy(backend_ip)) {
                available_servers.push_back({static_cast<unsigned int>(i), backend});
            }
        }
        
        if (available_servers.empty()) {
            std::cerr << "❌ No healthy backends available" << std::endl;
            return "";
        }
        
        // Use the dnsdist load balancing policy to select a server
        try {
            // Create a minimal DNSQuestion context (nullptr for now, as we don't need full context)
            DNSQuestion* dq = nullptr;
            
            auto selected_pos = applyPolicy(available_servers, dq);
            
            if (selected_pos.has_value() && *selected_pos < available_servers.size()) {
                auto& selected_backend = available_servers[*selected_pos].second;
                std::string selected_ip = getBackendIP(selected_backend);
                
                // Update statistics
                incrementBackendQueries(selected_backend);
                
                std::cout << "🎯 Policy '" << current_policy_name_ 
                          << "' selected: " << selected_ip 
                          << " (backend " << *selected_pos << ")" << std::endl;
                
                return selected_ip;
            }
        } catch (const std::exception& e) {
            std::cerr << "❌ Error applying load balancing policy: " << e.what() << std::endl;
        }
        
        // Fallback to first available server if policy fails
        if (!available_servers.empty()) {
            auto& fallback = available_servers[0].second;
            std::string fallback_ip = getBackendIP(fallback);
            std::cout << "⚠️  Fallback to first available: " << fallback_ip << std::endl;
            return fallback_ip;
        }
        
        return "";
    }
    
    /**
     * Change the load balancing policy
     * Supported: roundrobin, leastOutstanding, wrandom, whashed, chashed, firstAvailable
     */
    void setPolicy(const std::string& policy_name) {
        if (policy_name == "roundrobin") {
            current_policy_ = roundrobin;
            current_policy_name_ = "roundrobin";
        } else if (policy_name == "leastOutstanding") {
            current_policy_ = leastOutstanding;
            current_policy_name_ = "leastOutstanding";
        } else if (policy_name == "wrandom") {
            current_policy_ = wrandom;
            current_policy_name_ = "wrandom";
        } else if (policy_name == "whashed") {
            current_policy_ = whashed;
            current_policy_name_ = "whashed";
        } else if (policy_name == "chashed") {
            current_policy_ = chashed;
            current_policy_name_ = "chashed";
        } else if (policy_name == "firstAvailable") {
            current_policy_ = firstAvailable;
            current_policy_name_ = "firstAvailable";
        } else {
            std::cerr << "⚠️  Unknown policy '" << policy_name << "', using roundrobin" << std::endl;
            current_policy_ = roundrobin;
            current_policy_name_ = "roundrobin";
        }
        
        std::cout << "📋 Load balancing policy set to: " << current_policy_name_ << std::endl;
    }
    
    /**
     * Print statistics about backend server usage
     */
    void printStats() const {
        std::cout << "\n📊 Load Balancer Statistics:" << std::endl;
        std::cout << "   Policy: " << current_policy_name_ << std::endl;
        std::cout << "   Total Backends: " << backends_.size() << std::endl;
        
        size_t healthy_count = 0;
        for (const auto& backend : backends_) {
            std::string ip = getBackendIP(backend);
            if (health_checker_->isHealthy(ip)) {
                healthy_count++;
            }
        }
        std::cout << "   Healthy Backends: " << healthy_count << std::endl;
        
        // Print per-backend stats
        for (size_t i = 0; i < backends_.size(); ++i) {
            std::string ip = getBackendIP(backends_[i]);
            bool is_healthy = health_checker_->isHealthy(ip);
            uint64_t queries = getBackendQueryCount(backends_[i]);
            
            std::cout << "   Backend " << i << ": " << ip 
                      << (is_healthy ? " ✓" : " ✗")
                      << " (" << queries << " queries)" << std::endl;
        }
    }

private:
    HealthChecker* health_checker_;
    std::vector<std::shared_ptr<DownstreamState>> backends_;
    std::function<std::optional<ServerPolicy::SelectedServerPosition>(
        const ServerPolicy::NumberedServerVector&, const DNSQuestion*)> current_policy_;
    std::string current_policy_name_;
    
    // Map to store backend IPs (since DownstreamState might not expose it directly)
    std::map<DownstreamState*, std::string> backend_ip_map_;
    std::map<DownstreamState*, std::atomic<uint64_t>> backend_query_count_;
    
    /**
     * Initialize backend servers from configuration
     */
    void initializeBackends(const std::vector<ServerPool>& pools) {
        for (const auto& pool : pools) {
            for (const auto& server_ip : pool.servers) {
                // Create a DownstreamState for each backend
                // Note: This is a simplified version. In production dnsdist,
                // DownstreamState is much more complex with connection pools, etc.
                auto backend = std::make_shared<DownstreamState>();
                
                // Store the IP address for this backend
                backend_ip_map_[backend.get()] = server_ip;
                backend_query_count_[backend.get()] = 0;
                
                backends_.push_back(backend);
                
                std::cout << "   Added backend: " << server_ip 
                          << " (pool: " << pool.name << ")" << std::endl;
            }
        }
    }
    
    /**
     * Apply the current load balancing policy
     */
    std::optional<ServerPolicy::SelectedServerPosition> applyPolicy(
        const ServerPolicy::NumberedServerVector& servers,
        DNSQuestion* dq) const {
        
        if (current_policy_) {
            return current_policy_(servers, dq);
        }
        
        // Fallback: return first server
        if (!servers.empty()) {
            return 0;
        }
        
        return std::nullopt;
    }
    
    /**
     * Get IP address for a backend
     */
    std::string getBackendIP(const std::shared_ptr<DownstreamState>& backend) const {
        auto it = backend_ip_map_.find(backend.get());
        if (it != backend_ip_map_.end()) {
            return it->second;
        }
        return "";
    }
    
    /**
     * Increment query count for a backend
     */
    void incrementBackendQueries(const std::shared_ptr<DownstreamState>& backend) {
        auto it = backend_query_count_.find(backend.get());
        if (it != backend_query_count_.end()) {
            it->second++;
        }
    }
    
    /**
     * Get query count for a backend
     */
    uint64_t getBackendQueryCount(const std::shared_ptr<DownstreamState>& backend) const {
        auto it = backend_query_count_.find(backend.get());
        if (it != backend_query_count_.end()) {
            return it->second.load();
        }
        return 0;
    }
};

/**
 * DNS Server with integrated dnsdist load balancing
 */
class DnsServer {
public:
    DnsServer(boost::asio::io_context& io_context, DnsdistLoadBalancer* load_balancer)
        : socket_(io_context, udp::endpoint(udp::v4(), DNS_PORT)),
          load_balancer_(load_balancer) {
        
        zone_dname_ = ldns_dname_new_frm_str(ZONE_NAME);
        if (!zone_dname_) {
            throw std::runtime_error("Failed to create zone dname");
        }
        if (!load_balancer_) {
            throw std::runtime_error("LoadBalancer is null");
        }
        
        start_receive();
    }
    
    ~DnsServer() {
        if (zone_dname_) {
            ldns_rdf_deep_free(zone_dname_);
        }
    }

private:
    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    std::array<uint8_t, 512> recv_buffer_;
    ldns_rdf* zone_dname_;
    DnsdistLoadBalancer* load_balancer_;
    
    void start_receive() {
        socket_.async_receive_from(
            boost::asio::buffer(recv_buffer_), remote_endpoint_,
            [this](boost::system::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0) {
                    handle_request(bytes_recvd);
                }
                start_receive();
            });
    }

    void handle_request(std::size_t length) {
        ldns_pkt* query_pkt;
        ldns_status status = ldns_wire2pkt(&query_pkt, recv_buffer_.data(), length);
        if (status != LDNS_STATUS_OK) return;

        ldns_pkt* resp_pkt = ldns_pkt_new();
        ldns_pkt_set_id(resp_pkt, ldns_pkt_id(query_pkt));
        ldns_pkt_set_qr(resp_pkt, true);  // response
        ldns_pkt_set_aa(resp_pkt, true);  // authoritative
        ldns_pkt_set_ra(resp_pkt, false);

        ldns_rr_list* qlist = ldns_pkt_question(query_pkt);
        ldns_pkt_push_rr_list(resp_pkt, LDNS_SECTION_QUESTION, ldns_rr_list_clone(qlist));

        // Process each question
        for (size_t i = 0; i < ldns_rr_list_rr_count(qlist); ++i) {
            ldns_rr* q = ldns_rr_list_rr(qlist, i);
            ldns_rdf* qname = ldns_rr_owner(q);
            ldns_rr_type qtype = ldns_rr_get_type(q);

            if (ldns_dname_compare(qname, zone_dname_) == 0 && qtype == LDNS_RR_TYPE_A) {
                // Get domain name as string for load balancer
                char* domain_str = ldns_rdf2str(qname);
                std::string domain = domain_str ? std::string(domain_str) : "";
                free(domain_str);
                
                // Get next server from load balancer using dnsdist policies
                std::string backend_ip = load_balancer_->getServerForQuery(domain);
                
                if (backend_ip.empty()) {
                    // No backend available, return SERVFAIL
                    std::cerr << "❌ No backend server available for query" << std::endl;
                    ldns_pkt_set_rcode(resp_pkt, LDNS_RCODE_SERVFAIL);
                } else {
                    // Create A record with backend server IP
                    ldns_rr* a_rr = ldns_rr_new();
                    ldns_rr_set_owner(a_rr, ldns_rdf_clone(qname));
                    ldns_rr_set_type(a_rr, LDNS_RR_TYPE_A);
                    ldns_rr_set_class(a_rr, LDNS_RR_CLASS_IN);
                    ldns_rr_set_ttl(a_rr, 300); // TTL 5min

                    ldns_rdf* ip_rdf;
                    if (ldns_str2rdf_a(&ip_rdf, backend_ip.c_str()) == LDNS_STATUS_OK) {
                        ldns_rr_push_rdf(a_rr, ip_rdf);
                        ldns_pkt_push_rr(resp_pkt, LDNS_SECTION_ANSWER, a_rr);
                        std::cout << "✅ Responding with backend IP: " << backend_ip << std::endl;
                    } else {
                        std::cerr << "❌ Failed to convert IP: " << backend_ip << std::endl;
                        ldns_rr_free(a_rr);
                        ldns_pkt_set_rcode(resp_pkt, LDNS_RCODE_SERVFAIL);
                    }
                }
            } else {
                // Not in zone → NXDOMAIN
                ldns_pkt_set_rcode(resp_pkt, LDNS_RCODE_NXDOMAIN);
            }
        }

        // Convert packet to wire format
        uint8_t* resp_wire = nullptr;
        size_t resp_len;
        ldns_status wire_status = ldns_pkt2wire(&resp_wire, resp_pkt, &resp_len);

        if (wire_status == LDNS_STATUS_OK && resp_wire) {
            socket_.send_to(boost::asio::buffer(resp_wire, resp_len), remote_endpoint_);
            free(resp_wire);
        }

        ldns_pkt_free(query_pkt);
        ldns_pkt_free(resp_pkt);
    }
};

// Global objects for signal handling
HealthChecker* g_health_checker = nullptr;
DnsdistLoadBalancer* g_load_balancer = nullptr;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down gracefully..." << std::endl;
    if (g_health_checker) {
        g_health_checker->stop();
    }
    if (g_load_balancer) {
        g_load_balancer->printStats();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    try {
        std::cout << "🚀 Starting DNS Load Balancer with PowerDNS/dnsdist algorithms..." << std::endl;
        
        // Parse command line arguments
        std::string policy_name = "roundrobin"; // default
        if (argc > 1) {
            policy_name = argv[1];
        }
        
        // Show current working directory for debugging
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::cout << "📂 Current working directory: " << cwd << std::endl;
        }
        
        // Load configuration
        std::vector<std::string> possible_config_paths = {
            "config.json",
            "../config.json",
            "build/config.json",
            "../build/config.json"
        };
        
        std::vector<ServerPool> pools;
        bool config_loaded = false;
        
        for (const auto& config_path : possible_config_paths) {
            std::cout << "🔍 Trying to load config from: " << config_path << std::endl;
            pools = ConfigLoader::loadBackends(config_path);
            if (!pools.empty()) {
                std::cout << "✅ Successfully loaded config from: " << config_path << std::endl;
                config_loaded = true;
                break;
            }
        }
        
        if (!config_loaded) {
            std::cout << "⚠️  No config file found, creating default test pool..." << std::endl;
            ServerPool default_pool;
            default_pool.name = "test-pool";
            default_pool.servers = {"192.168.1.100", "192.168.1.101", "192.168.1.102"};
            default_pool.health_endpoint = "http://192.168.1.100/health";
            default_pool.geo_region = "us-east";
            default_pool.check_interval_sec = 10;
            pools.push_back(default_pool);
        }
        
        // Initialize and start health checker
        std::cout << "\n🏥 Initializing health checker..." << std::endl;
        HealthChecker health_checker(pools);
        g_health_checker = &health_checker;
        health_checker.start();
        
        // Initialize load balancer with dnsdist algorithms
        std::cout << "\n⚖️  Initializing dnsdist load balancer..." << std::endl;
        DnsdistLoadBalancer load_balancer(pools, &health_checker);
        g_load_balancer = &load_balancer;
        
        // Set the load balancing policy
        load_balancer.setPolicy(policy_name);
        
        // Start DNS server with load balancer
        std::cout << "\n🌐 Starting DNS server..." << std::endl;
        boost::asio::io_context io_context;
        DnsServer server(io_context, &load_balancer);
        std::cout << "✅ DNS server started on port " << DNS_PORT << std::endl;
        std::cout << "✅ Health checker monitoring " << pools.size() << " server pools" << std::endl;
        
        // Give health checker time to start
        std::this_thread::sleep_for(std::chrono::seconds(2));
        health_checker.printHealthSummary();
        
        // Start DNS server threads
        std::vector<std::thread> threads;
        const int num_threads = 4;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&io_context]() { 
                io_context.run(); 
            });
        }
        
        std::cout << "\n🎯 DNS Load Balancer is running!" << std::endl;
        std::cout << "   Policy: " << policy_name << std::endl;
        std::cout << "   Threads: " << num_threads << std::endl;
        std::cout << "   Press Ctrl+C to stop." << std::endl;
        std::cout << "\nAvailable policies:" << std::endl;
        std::cout << "   - roundrobin: Distribute queries evenly across backends" << std::endl;
        std::cout << "   - leastOutstanding: Send to backend with fewest pending queries" << std::endl;
        std::cout << "   - wrandom: Weighted random selection" << std::endl;
        std::cout << "   - whashed: Weighted consistent hashing" << std::endl;
        std::cout << "   - chashed: Consistent hashing" << std::endl;
        std::cout << "   - firstAvailable: Always use first available backend" << std::endl;
        
        // Wait for all threads
        for (auto& t : threads) {
            t.join();
        }
        
    } catch (std::exception& e) {
        std::cerr << "❌ Exception: " << e.what() << std::endl;
        if (g_health_checker) {
            g_health_checker->stop();
        }
        return 1;
    }
    
    return 0;
}
