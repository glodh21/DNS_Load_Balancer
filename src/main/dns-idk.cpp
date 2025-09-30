#include <boost/asio.hpp>
#include <ldns/ldns.h>
#include <iostream>
#include <thread>
#include <vector>

using boost::asio::ip::udp;

const int DNS_PORT = 5353; // 53 if root
const char* ZONE_NAME = "example.com."; // Our zone
const char* RESPONSE_IP = "1.2.3.4";

class DnsServer {
public:
    DnsServer(boost::asio::io_context& io_context)
        : socket_(io_context, udp::endpoint(udp::v4(), DNS_PORT)) {
        zone_dname_ = ldns_dname_new_frm_str(ZONE_NAME);
        if (!zone_dname_) {
            throw std::runtime_error("Failed to create zone dname");
        }
        start_receive();
    }
    ~DnsServer() {
        if (zone_dname_) {
            ldns_rdf_deep_free(zone_dname_);
        }
    }


private:
    ldns_rdf* zone_dname_;
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
    ldns_pkt_set_qr(resp_pkt, true); // response
    ldns_pkt_set_aa(resp_pkt, true); // authoritative
    ldns_pkt_set_ra(resp_pkt, false);

    ldns_rr_list* qlist = ldns_pkt_question(query_pkt);
    ldns_pkt_push_rr_list(resp_pkt, LDNS_SECTION_QUESTION, ldns_rr_list_clone(qlist));

    // For each question, respond with hardcoded A if in our zone
    for (size_t i = 0; i < ldns_rr_list_rr_count(qlist); ++i) {
        ldns_rr* q = ldns_rr_list_rr(qlist, i);
        ldns_rdf* qname = ldns_rr_owner(q);
        ldns_rr_type qtype = ldns_rr_get_type(q);

        if (ldns_dname_compare(qname, zone_dname_) == 0 && qtype == LDNS_RR_TYPE_A) {
            ldns_rr* a_rr = ldns_rr_new();
            ldns_rr_set_owner(a_rr, ldns_rdf_clone(qname));
            ldns_rr_set_type(a_rr, LDNS_RR_TYPE_A);
            ldns_rr_set_class(a_rr, LDNS_RR_CLASS_IN);
            ldns_rr_set_ttl(a_rr, 300); // TTL 5min

            ldns_rdf* ip_rdf;
            ldns_str2rdf_a(&ip_rdf, RESPONSE_IP);
            ldns_rr_push_rdf(a_rr, ip_rdf);

            ldns_pkt_push_rr(resp_pkt, LDNS_SECTION_ANSWER, a_rr);
        } else {
            // Not in zone → NXDOMAIN
            ldns_pkt_set_rcode(resp_pkt, LDNS_RCODE_NXDOMAIN);
        }
    }

    // Convert packet to wire format - CORRECTED
    uint8_t* resp_wire = nullptr;
    size_t resp_len;
    ldns_status wire_status = ldns_pkt2wire(&resp_wire, resp_pkt, &resp_len);

    if (wire_status == LDNS_STATUS_OK && resp_wire) {
        socket_.send_to(boost::asio::buffer(resp_wire, resp_len), remote_endpoint_);
        free(resp_wire); // Free the allocated wire data
    }

    ldns_pkt_free(query_pkt);
    ldns_pkt_free(resp_pkt);
}
    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    std::array<uint8_t, 512> recv_buffer_;
};

int main() {
    try {
        boost::asio::io_context io_context;
        DnsServer server(io_context);

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i)
            threads.emplace_back([&io_context]() { io_context.run(); });

        for (auto& t : threads)
            t.join();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
