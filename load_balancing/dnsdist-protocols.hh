#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace dnsdist
{
class Protocol
{
public:
  enum typeenum : uint8_t
  {
    DoUDP = 0,
    DoTCP,
    DNSCryptUDP,
    DNSCryptTCP,
    DoT,
    DoH,
    DoQ,
    DoH3
  };

  Protocol(typeenum protocol = DoUDP) :
    d_protocol(protocol)
  {
    if (protocol >= s_names.size()) {
      throw std::runtime_error("Unknown protocol: '" + std::to_string(protocol) + "'");
    }
  }

  explicit Protocol(const std::string& protocol);

  bool operator==(typeenum) const;
  bool operator!=(typeenum) const;
  bool operator==(const Protocol& rhs) const;
  bool operator!=(const Protocol& rhs) const;

  const std::string& toString() const;
  const std::string& toPrettyString() const;
  bool isUDP() const;
  bool isEncrypted() const;
  uint8_t toNumber() const;

private:
  typeenum d_protocol;

  static constexpr size_t s_numberOfProtocols = 8;
  static const std::array<std::string, s_numberOfProtocols> s_names;
  static const std::array<std::string, s_numberOfProtocols> s_prettyNames;
};
}