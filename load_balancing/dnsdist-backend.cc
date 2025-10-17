/*
 * This file is part of PowerDNS or dnsdist.
 * Copyright -- PowerDNS.COM B.V. and its contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * In addition, for the avoidance of any doubt, permission is granted to
 * link this program with OpenSSL and to (re)distribute the binaries
 * produced as the result of such linking.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

// for OpenBSD, sys/socket.h needs to come before net/if.h
#include <sys/socket.h>
#include <net/if.h>

#include <boost/format.hpp>

#include "config.h"
#include "dnsdist.hh"
#include "dnsdist-backend.hh"
// HEADERS REMOVED:
// #include "dnsdist-backoff.hh"
// #include "dnsdist-metrics.hh"
// #include "dnsdist-nghttp2.hh"
// #include "dnsdist-random.hh"
// #include "dnsdist-snmp.hh"
// #include "dnsdist-tcp.hh"
// #include "dnsdist-xsk.hh"
// #include "dolog.hh"
// #include "xsk.hh"

bool DownstreamState::passCrossProtocolQuery(std::unique_ptr<CrossProtocolQuery>&& cpq)
{
// HTTP/2 SUPPORT REMOVED
// #if defined(HAVE_DNS_OVER_HTTPS) && defined(HAVE_NGHTTP2)
//   if (!d_config.d_dohPath.empty()) {
//     return g_dohClientThreads && g_dohClientThreads->passCrossProtocolQueryToThread(std::move(cpq));
//   }
// #endif
// TCP CLIENT THREADS REMOVED
//   return g_tcpclientthreads && g_tcpclientthreads->passCrossProtocolQueryToThread(std::move(cpq));
  return false; // Simplified - cross-protocol queries disabled
}

// XSK SUPPORT COMPLETELY REMOVED
// #ifdef HAVE_XSK
// void DownstreamState::addXSKDestination(int fd) { ... }
// void DownstreamState::removeXSKDestination(int fd) { ... }
// #endif /* HAVE_XSK */

bool DownstreamState::reconnect(bool initialAttempt)
{
  std::unique_lock<std::mutex> lock(connectLock, std::try_to_lock);
  if (!lock.owns_lock() || isStopped()) {
    /* we are already reconnecting or stopped anyway */
    return false;
  }

  if (IsAnyAddress(d_config.remote)) {
    return true;
  }

  connected = false;

  for (auto& fd : sockets) {
    if (fd != -1) {
      if (sockets.size() > 1) {
        (*mplexer.lock())->removeReadFD(fd);
      }
      /* shutdown() is needed to wake up recv() in the responderThread */
      shutdown(fd, SHUT_RDWR);
      close(fd);
      fd = -1;
    }
    fd = SSocket(d_config.remote.sin4.sin_family, SOCK_DGRAM, 0);

#ifdef SO_BINDTODEVICE
    if (!d_config.sourceItfName.empty()) {
      int res = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, d_config.sourceItfName.c_str(), d_config.sourceItfName.length());
      if (res != 0) {
        // LOGGING REMOVED
        // infolog("Error setting up the interface on backend socket '%s': %s", d_config.remote.toStringWithPort(), stringerror());
      }
    }
#endif

    if (!IsAnyAddress(d_config.sourceAddr)) {
#ifdef IP_BIND_ADDRESS_NO_PORT
      if (d_config.ipBindAddrNoPort) {
        SSetsockopt(fd, SOL_IP, IP_BIND_ADDRESS_NO_PORT, 1);
      }
#endif
      SBind(fd, d_config.sourceAddr);
    }

    try {
      setDscp(fd, d_config.remote.sin4.sin_family, d_config.dscp);
      SConnect(fd, d_config.remote);
      if (sockets.size() > 1) {
        (*mplexer.lock())->addReadFD(fd, [](int, boost::any) {});
      }
      connected = true;
    }
    catch (const std::runtime_error& error) {
      // LOGGING AND VERBOSE CHECKS REMOVED
      connected = false;
      break;
    }
  }

  /* if at least one (re-)connection failed, close all sockets */
  if (!connected) {
    for (auto& fd : sockets) {
      if (fd != -1) {
        if (sockets.size() > 1) {
          try {
            (*mplexer.lock())->removeReadFD(fd);
          }
          catch (const FDMultiplexerException& e) {
            /* some sockets might not have been added to the multiplexer
               yet, that's fine */
          }
        }
        /* shutdown() is needed to wake up recv() in the responderThread */
        shutdown(fd, SHUT_RDWR);
        close(fd);
        fd = -1;
      }
    }
  }

  if (connected) {
    lock.unlock();
    d_connectedWait.notify_all();
    if (!initialAttempt) {
      /* we need to be careful not to start this
         thread too soon, as the creation should only
         happen after the configuration has been parsed */
      start();
    }
  }

  return connected;
}

void DownstreamState::waitUntilConnected()
{
  if (d_stopped) {
    return;
  }
  if (connected) {
    return;
  }
  {
    std::unique_lock<std::mutex> lock(connectLock);
    d_connectedWait.wait(lock, [this]{
      return connected.load();
    });
  }
}

void DownstreamState::stop()
{
  if (d_stopped) {
    return;
  }
  d_stopped = true;

  {
    auto tlock = std::scoped_lock(connectLock);
    auto slock = mplexer.lock();

    for (auto& fd : sockets) {
      if (fd != -1) {
        /* shutdown() is needed to wake up recv() in the responderThread */
        shutdown(fd, SHUT_RDWR);
      }
    }
  }
}

void DownstreamState::hash()
{
  const auto hashPerturbation = dnsdist::configuration::getImmutableConfiguration().d_hashPerturbation;
  // LOGGING REMOVED
  // vinfolog("Computing hashes for id=%s and weight=%d, hash_perturbation=%d", *d_config.id, d_config.d_weight, hashPerturbation);
  auto weight = d_config.d_weight;
  auto idStr = boost::str(boost::format("%s") % *d_config.id);
  auto lockedHashes = hashes.write_lock();
  lockedHashes->clear();
  lockedHashes->reserve(weight);
  while (weight > 0) {
    std::string uuid = boost::str(boost::format("%s-%d") % idStr % weight);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sorry, it's the burtle API
    unsigned int wshash = burtleCI(reinterpret_cast<const unsigned char*>(uuid.c_str()), uuid.size(), hashPerturbation);
    lockedHashes->push_back(wshash);
    --weight;
  }
  std::sort(lockedHashes->begin(), lockedHashes->end());
  hashesComputed = true;
}

void DownstreamState::setId(const boost::uuids::uuid& newId)
{
  d_config.id = newId;
  // compute hashes only if already done
  if (hashesComputed) {
    hash();
  }
}

void DownstreamState::setWeight(int newWeight)
{
  if (newWeight < 1) {
    // LOGGING REMOVED
    // errlog("Error setting server's weight: downstream weight value must be greater than 0.");
    return ;
  }

  d_config.d_weight = newWeight;

  if (hashesComputed) {
    hash();
  }
}

DownstreamState::DownstreamState(DownstreamState::Config&& config, std::shared_ptr<TLSCtx> tlsCtx, bool connect): d_config(std::move(config)), d_tlsCtx(std::move(tlsCtx))
{
  threadStarted.clear();

  if (d_config.d_qpsLimit > 0) {
    d_qpsLimiter = QPSLimiter(d_config.d_qpsLimit, d_config.d_qpsLimit);
  }

  if (d_config.id) {
    setId(*d_config.id);
  }
  else {
    d_config.id = getUniqueID();
  }

  if (d_config.d_weight > 0) {
    setWeight(d_config.d_weight);
  }

  // HEALTH CHECK INITIALIZATION REMOVED
  // if (d_config.d_availability == Availability::Auto && d_config.d_healthCheckMode == HealthCheckMode::Lazy && d_config.d_lazyHealthCheckSampleSize > 0) {
  //   d_lazyHealthCheckStats.lock()->d_lastResults.set_capacity(d_config.d_lazyHealthCheckSampleSize);
  //   setUpStatus(true);
  // }

  setName(d_config.name);

  // HTTP/2 OUTGOING WORKER THREADS REMOVED
  // if (d_tlsCtx && !d_config.d_dohPath.empty()) {
  // #ifdef HAVE_NGHTTP2
  //   auto outgoingDoHWorkerThreads = dnsdist::configuration::getImmutableConfiguration().d_outgoingDoHWorkers;
  //   if (dnsdist::configuration::isImmutableConfigurationDone() && outgoingDoHWorkerThreads && *outgoingDoHWorkerThreads == 0) {
  //     throw std::runtime_error("Error: setOutgoingDoHWorkerThreads() is set to 0 so no outgoing DoH worker thread is available to serve queries");
  //   }
  //
  //   if (!dnsdist::configuration::isImmutableConfigurationDone() && (!outgoingDoHWorkerThreads || *outgoingDoHWorkerThreads == 0)) {
  //     dnsdist::configuration::updateImmutableConfiguration([](dnsdist::configuration::ImmutableConfiguration& immutableConfig) {
  //       immutableConfig.d_outgoingDoHWorkers = 1;
  //     });
  //   }
  // #endif /* HAVE_NGHTTP2 */
  // }

  if (connect && !isTCPOnly()) {
    if (!IsAnyAddress(d_config.remote)) {
      connectUDPSockets();
    }
  }

  sw.start();
}

void DownstreamState::start()
{
  if (connected && !threadStarted.test_and_set()) {
    // XSK RESPONDER THREADS REMOVED
    // #ifdef HAVE_XSK
    // for (auto& xskInfo : d_xskInfos) {
    //   auto xskResponderThread = std::thread(dnsdist::xsk::XskResponderThread, shared_from_this(), xskInfo);
    //   if (!d_config.d_cpus.empty()) {
    //     mapThreadToCPUList(xskResponderThread.native_handle(), d_config.d_cpus);
    //   }
    //   xskResponderThread.detach();
    // }
    // #endif /* HAVE_XSK */

    auto tid = std::thread(responderThread, shared_from_this());
    if (!d_config.d_cpus.empty()) {
      mapThreadToCPUList(tid.native_handle(), d_config.d_cpus);
    }
    tid.detach();
  }
}

void DownstreamState::connectUDPSockets()
{
  const auto& config = dnsdist::configuration::getImmutableConfiguration();
  if (config.d_randomizeIDsToBackend) {
    idStates.clear();
  }
  else {
    idStates.resize(config.d_maxUDPOutstanding);
  }
  sockets.resize(d_config.d_numberOfSockets);

  if (sockets.size() > 1) {
    *(mplexer.lock()) = std::unique_ptr<FDMultiplexer>(FDMultiplexer::getMultiplexerSilent(sockets.size()));
  }

  for (auto& fd : sockets) {
    fd = -1;
  }

  reconnect(true);
}

DownstreamState::~DownstreamState()
{
  for (auto& fd : sockets) {
    if (fd >= 0) {
      close(fd);
      fd = -1;
    }
  }
}

void DownstreamState::incCurrentConnectionsCount()
{
  auto currentConnectionsCount = ++tcpCurrentConnections;
  if (currentConnectionsCount > tcpMaxConcurrentConnections) {
    tcpMaxConcurrentConnections.store(currentConnectionsCount);
  }
}

int DownstreamState::pickSocketForSending()
{
  size_t numberOfSockets = sockets.size();
  if (numberOfSockets == 1) {
    return sockets[0];
  }

  size_t idx{0};
  if (dnsdist::configuration::getImmutableConfiguration().d_randomizeUDPSocketsToBackend) {
    // RANDOM VALUE GENERATION REMOVED - using simple round-robin
    idx = socketsOffset % numberOfSockets;
  }
  else {
    idx = socketsOffset++;
  }

  return sockets[idx % numberOfSockets];
}

void DownstreamState::pickSocketsReadyForReceiving(std::vector<int>& ready)
{
  ready.clear();

  if (sockets.size() == 1) {
    ready.push_back(sockets[0]);
    return ;
  }

  (*mplexer.lock())->getAvailableFDs(ready, 1000);
}

static bool isIDSExpired(const IDState& ids, uint8_t udpTimeout)
{
  auto age = ids.age.load();
  return age > udpTimeout;
}

void DownstreamState::handleUDPTimeout(IDState& ids)
{
  ids.age = 0;
  ids.inUse = false;
  ++reuseds;
  --outstanding;
  // METRICS REMOVED
  // ++dnsdist::metrics::g_stats.downstreamTimeouts;
  // LOGGING REMOVED
  // vinfolog("Had a downstream timeout from %s (%s) for query for %s|%s from %s",
  //          d_config.remote.toStringWithPort(), getName(),
  //          ids.internal.qname.toLogString(), QType(ids.internal.qtype).toString(), ids.internal.origRemote.toStringWithPort());

  const auto& chains = dnsdist::configuration::getCurrentRuntimeConfiguration().d_ruleChains;
  const auto& timeoutRespRules = dnsdist::rules::getResponseRuleChain(chains, dnsdist::rules::ResponseRuleChain::TimeoutResponseRules);
  auto sender = ids.internal.du == nullptr ? nullptr : ids.internal.du->getQuerySender();
  if (!handleTimeoutResponseRules(timeoutRespRules, ids.internal, shared_from_this(), sender)) {
    DOHUnitInterface::handleTimeout(std::move(ids.internal.du));
  }

  if (g_rings.shouldRecordResponses()) {
    timespec now{};
    gettime(&now);

    dnsheader fake{};
    memset(&fake, 0, sizeof(fake));
    fake.id = ids.internal.origID;
    uint16_t* flags = getFlagsFromDNSHeader(&fake);
    *flags = ids.internal.origFlags;

    g_rings.insertResponse(now, ids.internal.origRemote, ids.internal.qname, ids.internal.qtype, std::numeric_limits<unsigned int>::max(), 0, fake, d_config.remote, getProtocol());
  }

  reportTimeoutOrError();
}

void DownstreamState::reportResponse(uint8_t rcode)
{
  // HEALTH CHECK REPORTING REMOVED
  // if (d_config.d_availability == Availability::Auto && d_config.d_healthCheckMode == HealthCheckMode::Lazy && d_config.d_lazyHealthCheckSampleSize > 0) {
  //   bool failure = d_config.d_lazyHealthCheckMode == LazyHealthCheckMode::TimeoutOrServFail ? rcode == RCode::ServFail : false;
  //   d_lazyHealthCheckStats.lock()->d_lastResults.push_back(failure);
  // }
}

void DownstreamState::reportTimeoutOrError()
{
  // HEALTH CHECK REPORTING REMOVED
  // if (d_config.d_availability == Availability::Auto && d_config.d_healthCheckMode == HealthCheckMode::Lazy && d_config.d_lazyHealthCheckSampleSize > 0) {
  //   d_lazyHealthCheckStats.lock()->d_lastResults.push_back(true);
  // }
}

void DownstreamState::handleUDPTimeouts()
{
  if (getProtocol() != dnsdist::Protocol::DoUDP) {
    return;
  }

  const auto& config = dnsdist::configuration::getImmutableConfiguration();
  const auto udpTimeout = d_config.udpTimeout > 0 ? d_config.udpTimeout : config.d_udpTimeout;
  if (config.d_randomizeIDsToBackend) {
    auto map = d_idStatesMap.lock();
    for (auto it = map->begin(); it != map->end(); ) {
      auto& ids = it->second;
      if (isIDSExpired(ids, udpTimeout)) {
        handleUDPTimeout(ids);
        it = map->erase(it);
        continue;
      }
      ++ids.age;
      ++it;
    }
  }
  else {
    if (outstanding.load() > 0) {
      for (IDState& ids : idStates) {
        if (!ids.isInUse()) {
          continue;
        }
        if (!isIDSExpired(ids, udpTimeout)) {
          ++ids.age;
          continue;
        }
        auto guard = ids.acquire();
        if (!guard) {
          continue;
        }
        /* check again, now that we have locked this state */
        if (ids.isInUse() && isIDSExpired(ids, udpTimeout)) {
          handleUDPTimeout(ids);
        }
      }
    }
  }
}

uint16_t DownstreamState::saveState(InternalQueryState&& state)
{
  const auto& config = dnsdist::configuration::getImmutableConfiguration();
  if (config.d_randomizeIDsToBackend) {
    /* if the state is already in use we will retry,
       up to 5 five times. The last selected one is used
       even if it was already in use */
    size_t remainingAttempts = 5;
    auto map = d_idStatesMap.lock();

    do {
      uint16_t selectedID = socketsOffset++ % std::numeric_limits<uint16_t>::max(); // Simple ID generation
      auto [it, inserted] = map->emplace(selectedID, IDState());

      if (!inserted) {
        remainingAttempts--;
        if (remainingAttempts > 0) {
          continue;
        }

        auto oldDU = std::move(it->second.internal.du);
        ++reuseds;
        // METRICS REMOVED
        // ++dnsdist::metrics::g_stats.downstreamTimeouts;
        DOHUnitInterface::handleTimeout(std::move(oldDU));
      }
      else {
        ++outstanding;
      }

      it->second.internal = std::move(state);
      it->second.age.store(0);

      return it->first;
    }
    while (true);
  }

  do {
    uint16_t selectedID = (idOffset++) % idStates.size();
    IDState& ids = idStates[selectedID];
    auto guard = ids.acquire();
    if (!guard) {
      continue;
    }
    if (ids.isInUse()) {
      /* we are reusing a state, no change in outstanding but if there was an existing DOHUnit we need
         to handle it because it's about to be overwritten. */
      auto oldDU = std::move(ids.internal.du);
      ++reuseds;
      // METRICS REMOVED
      // ++dnsdist::metrics::g_stats.downstreamTimeouts;
      DOHUnitInterface::handleTimeout(std::move(oldDU));
    }
    else {
      ++outstanding;
    }
    ids.internal = std::move(state);
    ids.age.store(0);
    ids.inUse = true;
    return selectedID;
  }
  while (true);
}

void DownstreamState::restoreState(uint16_t id, InternalQueryState&& state)
{
  const auto& config = dnsdist::configuration::getImmutableConfiguration();
  if (config.d_randomizeIDsToBackend) {
    auto map = d_idStatesMap.lock();

    auto [it, inserted] = map->emplace(id, IDState());
    if (!inserted) {
      /* already used */
      ++reuseds;
      // METRICS REMOVED
      // ++dnsdist::metrics::g_stats.downstreamTimeouts;
      DOHUnitInterface::handleTimeout(std::move(state.du));
    }
    else {
      it->second.internal = std::move(state);
      ++outstanding;
    }
    return;
  }

  auto& ids = idStates[id];
  auto guard = ids.acquire();
  if (!guard) {
    /* already used */
    ++reuseds;
    // METRICS REMOVED
    // ++dnsdist::metrics::g_stats.downstreamTimeouts;
    DOHUnitInterface::handleTimeout(std::move(state.du));
    return;
  }
  if (ids.isInUse()) {
    /* already used */
    ++reuseds;
    // METRICS REMOVED
    // ++dnsdist::metrics::g_stats.downstreamTimeouts;
    DOHUnitInterface::handleTimeout(std::move(state.du));
    return;
  }
  ids.internal = std::move(state);
  ids.inUse = true;
  ++outstanding;
}

std::optional<InternalQueryState> DownstreamState::getState(uint16_t id)
{
  std::optional<InternalQueryState> result = std::nullopt;
  const auto& config = dnsdist::configuration::getImmutableConfiguration();
  if (config.d_randomizeIDsToBackend) {
    auto map = d_idStatesMap.lock();

    auto it = map->find(id);
    if (it == map->end()) {
      return result;
    }

    result = std::move(it->second.internal);
    map->erase(it);
    --outstanding;
    return result;
  }

  if (id > idStates.size()) {
    return result;
  }

  auto& ids = idStates[id];
  auto guard = ids.acquire();
  if (!guard) {
    return result;
  }

  if (ids.isInUse()) {
    result = std::move(ids.internal);
    --outstanding;
  }
  ids.inUse = false;
  return result;
}

// COMPLETELY REMOVED HEALTH CHECK FUNCTIONS:
// bool DownstreamState::healthCheckRequired(std::optional<time_t> currentTime) { ... }
// time_t DownstreamState::getNextLazyHealthCheck() { ... }
// void DownstreamState::updateNextLazyHealthCheck(LazyHealthCheckStats& stats, bool checkScheduled, std::optional<time_t> currentTime) { ... }
// void DownstreamState::submitHealthCheckResult(bool initial, bool newResult) { ... }

// COMPLETELY REMOVED XSK FUNCTIONS:
// #ifdef HAVE_XSK
// [[nodiscard]] ComboAddress DownstreamState::pickSourceAddressForSending() { ... }
// void DownstreamState::registerXsk(std::vector<std::shared_ptr<XskSocket>>& xsks) { ... }
// #endif /* HAVE_XSK */

bool DownstreamState::parseSourceParameter(const std::string& source, DownstreamState::Config& config)
{
  /* handle source in the following forms:
     - v4 address ("192.0.2.1")
     - v6 address ("2001:DB8::1")
     - interface name ("eth0")
     - v4 address and interface name ("192.0.2.1@eth0")
     - v6 address and interface name ("2001:DB8::1@eth0")
  */
  std::string::size_type pos = source.find('@');
  if (pos == std::string::npos) {
    /* no '@', try to parse that as a valid v4/v6 address */
    try {
      config.sourceAddr = ComboAddress(source);
      return true;
    }
    catch (...) {
    }
  }

  /* try to parse as interface name, or v4/v6@itf */
  config.sourceItfName = source.substr(pos == std::string::npos ? 0 : pos + 1);
  unsigned int itfIdx = if_nametoindex(config.sourceItfName.c_str());
  if (itfIdx != 0) {
    if (pos == 0 || pos == std::string::npos) {
      /* "eth0" or "@eth0" */
      config.sourceItf = itfIdx;
    }
    else {
      /* "192.0.2.1@eth0" */
      config.sourceAddr = ComboAddress(source.substr(0, pos));
      config.sourceItf = itfIdx;
    }
#ifdef SO_BINDTODEVICE
    if (!dnsdist::configuration::isImmutableConfigurationDone()) {
      /* we need to retain CAP_NET_RAW to be able to set SO_BINDTODEVICE in the health checks */
      dnsdist::configuration::updateImmutableConfiguration([](dnsdist::configuration::ImmutableConfiguration& currentConfig) {
        currentConfig.d_capabilitiesToRetain.insert("CAP_NET_RAW");
      });
    }
#endif
    return true;
  }

  // LOGGING REMOVED
  // warnlog("Dismissing source %s because '%s' is not a valid interface name", source, config.sourceItfName);
  return false;
}

bool DownstreamState::parseAvailabilityConfigFromStr(DownstreamState::Config& config, const std::string& str)
{
  if (pdns_iequals(str, "auto")) {
    config.d_availability = DownstreamState::Availability::Auto;
    config.d_healthCheckMode = DownstreamState::HealthCheckMode::Active;
    return true;
  }
  if (pdns_iequals(str, "lazy")) {
    config.d_availability = DownstreamState::Availability::Auto;
    config.d_healthCheckMode = DownstreamState::HealthCheckMode::Lazy;
    return true;
  }
  if (pdns_iequals(str, "up")) {
    config.d_availability = DownstreamState::Availability::Up;
    return true;
  }
  if (pdns_iequals(str, "down")) {
    config.d_availability = DownstreamState::Availability::Down;
    return true;
  }
  return false;
}

unsigned int DownstreamState::getQPSLimit() const
{
  return d_qpsLimiter ? d_qpsLimiter->getRate() : 0U;
}

size_t ServerPool::countServers(bool upOnly) const
{
  size_t count = 0;
  for (const auto& server : d_servers) {
    if (!upOnly || std::get<1>(server)->isUp() ) {
      count++;
    }
  }

  return count;
}

size_t ServerPool::poolLoad() const
{
  size_t load = 0;
  for (const auto& server : d_servers) {
    size_t serverOutstanding = std::get<1>(server)->outstanding.load();
    load += serverOutstanding;
  }
  return load;
}

bool ServerPool::hasAtLeastOneServerAvailable() const
{
  // NOLINTNEXTLINE(readability-use-anyofallof): no it's not more readable
  for (const auto& server : d_servers) {
    if (std::get<1>(server)->isUp()) {
      return true;
    }
  }
  return false;
}

const ServerPolicy::NumberedServerVector& ServerPool::getServers() const
{
  return d_servers;
}

void ServerPool::addServer(std::shared_ptr<DownstreamState>& server)
{
  auto count = static_cast<unsigned int>(d_servers.size());
  d_servers.emplace_back(++count, server);
  /* we need to reorder based on the server 'order' */
  std::stable_sort(d_servers.begin(), d_servers.end(), [](const std::pair<unsigned int,std::shared_ptr<DownstreamState> >& lhs, const std::pair<unsigned int,std::shared_ptr<DownstreamState> >& rhs) {
      return lhs.second->d_config.order < rhs.second->d_config.order;
    });
  /* and now we need to renumber for Lua (custom policies) */
  size_t idx = 1;
  for (auto& serv : d_servers) {
    serv.first = idx++;
  }

  updateConsistency();
}

void ServerPool::removeServer(shared_ptr<DownstreamState>& server)
{
  size_t idx = 1;
  bool found = false;
  for (auto it = d_servers.begin(); it != d_servers.end();) {
    if (found) {
      /* we need to renumber the servers placed
         after the removed one, for Lua (custom policies) */
      it->first = idx++;
      it++;
    }
    else if (it->second == server) {
      it = d_servers.erase(it);
      found = true;
    } else {
      idx++;
      it++;
    }
  }

  if (found && !d_isConsistent) {
    updateConsistency();
  }
}

void ServerPool::updateConsistency()
{
  bool consistent{true};
  bool first{true};
  bool useECS{false};
  bool tcpOnly{false};
  bool zeroScope{true};

  for (const auto& serverPair : d_servers) {
    const auto& server = serverPair.second;
    if (first) {
      first = false;
      useECS = server->d_config.useECS;
      tcpOnly = server->isTCPOnly();
      zeroScope = !server->d_config.disableZeroScope;
      continue;
    }
    if (consistent) {
      if (server->d_config.useECS != useECS) {
        consistent = false;
      }
      if (server->d_config.disableZeroScope == zeroScope) {
        consistent = false;
      }
    }
    if (server->isTCPOnly() != tcpOnly) {
      consistent = false;
      tcpOnly = false;
    }
  }

  d_tcpOnly = tcpOnly;
  if (consistent) {
    /* at this point we know that all servers agree
       on these settings, so let's just use the same
       values for the pool itself */
    d_useECS = useECS;
    d_zeroScope = zeroScope;
  }
  d_isConsistent = consistent;
}

void ServerPool::setZeroScope(bool enabled)
{
  d_zeroScope = enabled;
  updateConsistency();
}

void ServerPool::setECS(bool useECS)
{
  d_useECS = useECS;
  updateConsistency();
}

namespace dnsdist::backend
{
void registerNewBackend(std::shared_ptr<DownstreamState>& backend)
{
  dnsdist::configuration::updateRuntimeConfiguration([&backend](dnsdist::configuration::RuntimeConfiguration& config) {
    auto& backends = config.d_backends;
    backends.push_back(backend);
    std::stable_sort(backends.begin(), backends.end(), [](const std::shared_ptr<DownstreamState>& lhs, const std::shared_ptr<DownstreamState>& rhs) {
      return lhs->d_config.order < rhs->d_config.order;
    });
  });
}
}