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

 #include <cstdint>
 #include <cstdio>
 #include <dirent.h>
 #include <fstream>
 #include <cinttypes>
 
 #include <regex>
 #include <sys/types.h>
 #include <sys/stat.h>
 #include <thread>
 #include <vector>
 
 #include "dnsdist.hh"
 #include "dnsdist-backend.hh"
 #include "dnsdist-configuration.hh"
 #include "dnsdist-console.hh"
 #include "dnsdist-lua.hh"
 #include "dnsdist-rings.hh"
 
 #include "base64.hh"
 #include "threadname.hh"
 
 #ifdef HAVE_LIBSSL
 #include "libssl.hh"
 #endif
 
 #include <boost/logic/tribool.hpp>
 #include <boost/uuid/string_generator.hpp>
 
 #ifdef HAVE_SYSTEMD
 #include <systemd/sd-daemon.h>
 #endif
 
 using std::thread;
 
 using update_metric_opts_t = LuaAssociativeTable<boost::variant<uint64_t, LuaAssociativeTable<std::string>>>;
 using declare_metric_opts_t = LuaAssociativeTable<boost::variant<bool, std::string>>;
 
 static boost::tribool s_noLuaSideEffect;
 
 /* this is a best effort way to prevent logging calls with no side-effects in the output of delta()
    Functions can declare setLuaNoSideEffect() and if nothing else does declare a side effect, or nothing
    has done so before on this invocation, this call won't be part of delta() output */
 void setLuaNoSideEffect()
 {
   if (s_noLuaSideEffect == false) {
     // there has been a side effect already
     return;
   }
   s_noLuaSideEffect = true;
 }
 
 void setLuaSideEffect()
 {
   s_noLuaSideEffect = false;
 }
 
 bool getLuaNoSideEffect()
 {
   if (s_noLuaSideEffect) {
     // NOLINTNEXTLINE(readability-simplify-boolean-expr): it's a tribool, not a boolean
     return true;
   }
   return false;
 }
 
 void resetLuaSideEffect()
 {
   s_noLuaSideEffect = boost::logic::indeterminate;
 }
 
 // Query Counting Implementation
 namespace dnsdist::QueryCount
 {
 struct QueryCountRecord
 {
   std::string key;
   uint64_t count{0};
 };
 
 using QueryCountRecords = std::vector<QueryCountRecord>;
 
 struct Configuration
 {
   using Filter = std::function<bool(const std::string& key)>;
   
   bool d_enabled{true};
   Filter d_filter{nullptr};
   size_t d_maxRecords{1000};
 };
 
 static Configuration g_queryCountConfig;
 static SharedLockGuarded<QueryCountRecords> g_queryCountRecords;
 
 void clear()
 {
   auto records = g_queryCountRecords.write_lock();
   records->clear();
 }
 
 void increment(const std::string& key)
 {
   if (!g_queryCountConfig.d_enabled) {
     return;
   }
   
   if (g_queryCountConfig.d_filter && !g_queryCountConfig.d_filter(key)) {
     return;
   }
   
   auto records = g_queryCountRecords.write_lock();
   
   // Find existing record
   for (auto& record : *records) {
     if (record.key == key) {
       record.count++;
       return;
     }
   }
   
   // Add new record if we have space
   if (records->size() < g_queryCountConfig.d_maxRecords) {
     records->push_back({key, 1});
   }
 }
 
 QueryCountRecords getRecords(uint64_t maxRecords = 0)
 {
   auto records = g_queryCountRecords.read_lock();
   QueryCountRecords result;
   
   if (maxRecords == 0 || maxRecords >= records->size()) {
     result = *records;
   } else {
     result.assign(records->begin(), records->begin() + maxRecords);
   }
   
   // Sort by count descending
   std::sort(result.begin(), result.end(), 
     [](const QueryCountRecord& a, const QueryCountRecord& b) {
       return a.count > b.count;
     });
   
   return result;
 }
 
 size_t getSize()
 {
   auto records = g_queryCountRecords.read_lock();
   return records->size();
 }
 
 void setConfiguration(const Configuration& config)
 {
   g_queryCountConfig = config;
 }
 
 Configuration getConfiguration()
 {
   return g_queryCountConfig;
 }
 }
 
 using localbind_t = LuaAssociativeTable<boost::variant<bool, int, std::string, LuaArray<int>, LuaArray<std::string>, LuaAssociativeTable<std::string>>>;
 
 static void parseLocalBindVars(boost::optional<localbind_t>& vars, bool& reusePort, int& tcpFastOpenQueueSize, std::string& interface, std::set<int>& cpus, int& tcpListenQueueSize, uint64_t& maxInFlightQueriesPerConnection, uint64_t& tcpMaxConcurrentConnections, bool& enableProxyProtocol)
 {
   if (vars) {
     LuaArray<int> setCpus;
 
     getOptionalValue<bool>(vars, "reusePort", reusePort);
     getOptionalValue<bool>(vars, "enableProxyProtocol", enableProxyProtocol);
     getOptionalValue<int>(vars, "tcpFastOpenQueueSize", tcpFastOpenQueueSize);
     getOptionalValue<int>(vars, "tcpListenQueueSize", tcpListenQueueSize);
     getOptionalValue<int>(vars, "maxConcurrentTCPConnections", tcpMaxConcurrentConnections);
     getOptionalValue<int>(vars, "maxInFlight", maxInFlightQueriesPerConnection);
     getOptionalValue<std::string>(vars, "interface", interface);
     if (getOptionalValue<decltype(setCpus)>(vars, "cpus", setCpus) > 0) {
       for (const auto& cpu : setCpus) {
         cpus.insert(cpu.second);
       }
     }
   }
 }
 
 #if defined(HAVE_DNS_OVER_TLS) || defined(HAVE_DNS_OVER_HTTPS) || defined(HAVE_DNS_OVER_QUIC)
 static bool loadTLSCertificateAndKeys(const std::string& context, std::vector<TLSCertKeyPair>& pairs, const boost::variant<std::string, std::shared_ptr<TLSCertKeyPair>, LuaArray<std::string>, LuaArray<std::shared_ptr<TLSCertKeyPair>>>& certFiles, const LuaTypeOrArrayOf<std::string>& keyFiles)
 {
   return true;
 }
 
 static void parseTLSConfig(TLSConfig& config, const std::string& context, boost::optional<localbind_t>& vars)
 {
   getOptionalValue<std::string>(vars, "ciphers", config.d_ciphers);
   getOptionalValue<std::string>(vars, "ciphersTLS13", config.d_ciphers13);
 }
 #endif // defined(HAVE_DNS_OVER_TLS) || defined(HAVE_DNS_OVER_HTTPS)
 
 void checkParameterBound(const std::string& parameter, uint64_t value, uint64_t max)
 {
   if (value > max) {
     throw std::runtime_error("The value (" + std::to_string(value) + ") passed to " + parameter + " is too large, the maximum is " + std::to_string(max));
   }
 }
 
 static void LuaThread(const std::string& code)
 {
   setThreadName("dnsdist/lua-bg");
   LuaContext context;
 
   // mask SIGTERM on threads so the signal always comes to dnsdist itself
   sigset_t blockSignals;
 
   sigemptyset(&blockSignals);
   sigaddset(&blockSignals, SIGTERM);
 
   pthread_sigmask(SIG_BLOCK, &blockSignals, nullptr);
 
   context.writeFunction("submitToMainThread", [](std::string cmd, LuaAssociativeTable<std::string> data) {
     auto lua = g_lua.lock();
     auto func = lua->readVariable<boost::optional<std::function<void(std::string cmd, LuaAssociativeTable<std::string> data)>>>("threadmessage");
     if (func) {
       func.get()(std::move(cmd), std::move(data));
     }
   });
 
   for (;;) {
     try {
       dnsdist::configuration::refreshLocalRuntimeConfiguration();
       context.executeCode(code);
     }
     catch (const std::exception& e) {
     }
     catch (...) {
     }
     std::this_thread::sleep_for(std::chrono::seconds(5));
   }
 }
 
 static bool checkConfigurationTime(const std::string& name)
 {
   if (!dnsdist::configuration::isImmutableConfigurationDone()) {
     return true;
   }
   g_outputBuffer = name + " cannot be used at runtime!\n";
   return false;
 }
 
 using newserver_t = LuaAssociativeTable<boost::variant<bool, std::string, LuaArray<std::string>>>;
 
 static void handleNewServerSourceParameter(boost::optional<newserver_t>& vars, DownstreamState::Config& config)
 {
   std::string source;
   if (getOptionalValue<std::string>(vars, "source", source) <= 0) {
     return;
   }
 
   DownstreamState::parseSourceParameter(source, config);
 }
 
 // NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
 static void setupLuaConfig(LuaContext& luaCtx, bool client, bool configCheck)
 {
   dnsdist::lua::setupConfigurationItems(luaCtx);
 
   luaCtx.writeFunction("newServer",
                        [client, configCheck](boost::variant<string, newserver_t> pvars, boost::optional<int> qps) {
                          setLuaSideEffect();
 
                          boost::optional<newserver_t> vars = newserver_t();
                          DownstreamState::Config config;
 
                          std::string serverAddressStr;
                          if (auto* addrStr = boost::get<string>(&pvars)) {
                            serverAddressStr = *addrStr;
                            if (qps) {
                              (*vars)["qps"] = std::to_string(*qps);
                            }
                          }
                          else {
                            vars = boost::get<newserver_t>(pvars);
                            getOptionalValue<std::string>(vars, "address", serverAddressStr);
                          }
 
                          handleNewServerSourceParameter(vars, config);
 
                          std::string valueStr;
                          if (getOptionalValue<std::string>(vars, "sockets", valueStr) > 0) {
                            config.d_numberOfSockets = std::stoul(valueStr);
                            if (config.d_numberOfSockets == 0) {
                              config.d_numberOfSockets = 1;
                            }
                          }
 
                          getOptionalIntegerValue("newServer", vars, "qps", config.d_qpsLimit);
                          getOptionalIntegerValue("newServer", vars, "order", config.order);
                          getOptionalIntegerValue("newServer", vars, "weight", config.d_weight);
                          if (config.d_weight < 1) {
                            return std::shared_ptr<DownstreamState>();
                          }
 
                          getOptionalIntegerValue("newServer", vars, "retries", config.d_retries);
                          getOptionalIntegerValue("newServer", vars, "tcpConnectTimeout", config.tcpConnectTimeout);
                          getOptionalIntegerValue("newServer", vars, "tcpSendTimeout", config.tcpSendTimeout);
                          getOptionalIntegerValue("newServer", vars, "tcpRecvTimeout", config.tcpRecvTimeout);
                          getOptionalIntegerValue("newServer", vars, "udpTimeout", config.udpTimeout);
 
                          bool fastOpen{false};
                          if (getOptionalValue<bool>(vars, "tcpFastOpen", fastOpen) > 0) {
                            if (fastOpen) {
 #ifdef MSG_FASTOPEN
                              config.tcpFastOpen = true;
 #else
 #endif
                            }
                          }
 
                          getOptionalIntegerValue("newServer", vars, "maxInFlight", config.d_maxInFlightQueriesPerConn);
                          getOptionalIntegerValue("newServer", vars, "maxConcurrentTCPConnections", config.d_tcpConcurrentConnectionsLimit);
 
                          getOptionalValue<std::string>(vars, "name", config.name);
 
                          if (getOptionalValue<std::string>(vars, "id", valueStr) > 0) {
                            config.id = boost::uuids::string_generator()(valueStr);
                          }
 
                          getOptionalValue<bool>(vars, "useProxyProtocol", config.useProxyProtocol);
                          getOptionalValue<bool>(vars, "proxyProtocolAdvertiseTLS", config.d_proxyProtocolAdvertiseTLS);
                          getOptionalValue<bool>(vars, "ipBindAddrNoPort", config.ipBindAddrNoPort);
 
                          getOptionalValue<bool>(vars, "reconnectOnUp", config.reconnectOnUp);
 
                          LuaArray<string> cpuMap;
                          if (getOptionalValue<decltype(cpuMap)>(vars, "cpus", cpuMap) > 0) {
                            for (const auto& cpu : cpuMap) {
                              config.d_cpus.insert(std::stoi(cpu.second));
                            }
                          }
 
                          getOptionalValue<bool>(vars, "tcpOnly", config.d_tcpOnly);
 
                          std::shared_ptr<TLSCtx> tlsCtx;
                          getOptionalValue<std::string>(vars, "ciphers", config.d_tlsParams.d_ciphers);
                          getOptionalValue<std::string>(vars, "ciphers13", config.d_tlsParams.d_ciphers13);
                          getOptionalValue<std::string>(vars, "caStore", config.d_tlsParams.d_caStore);
                          getOptionalValue<bool>(vars, "validateCertificates", config.d_tlsParams.d_validateCertificates);
                          getOptionalValue<bool>(vars, "releaseBuffers", config.d_tlsParams.d_releaseBuffers);
                          getOptionalValue<bool>(vars, "enableRenegotiation", config.d_tlsParams.d_enableRenegotiation);
                          getOptionalValue<bool>(vars, "ktls", config.d_tlsParams.d_ktls);
                          getOptionalValue<std::string>(vars, "subjectName", config.d_tlsSubjectName);
                          getOptionalIntegerValue("newServer", vars, "dscp", config.dscp);
 
                          if (getOptionalValue<std::string>(vars, "subjectAddr", valueStr) > 0) {
                            try {
                              ComboAddress addr(valueStr);
                              config.d_tlsSubjectName = addr.toString();
                              config.d_tlsSubjectIsAddr = true;
                            }
                            catch (const std::exception&) {
                              return std::shared_ptr<DownstreamState>();
                            }
                          }
 
                          uint16_t serverPort = 53;
 
                          if (getOptionalValue<std::string>(vars, "tls", valueStr) > 0) {
                            serverPort = 853;
                            config.d_tlsParams.d_provider = valueStr;
                          }
 
                          try {
                            config.remote = ComboAddress(serverAddressStr, serverPort);
                          }
                          catch (const PDNSException& e) {
                            g_outputBuffer = "Error creating new server: " + string(e.reason);
                            return std::shared_ptr<DownstreamState>();
                          }
                          catch (const std::exception& e) {
                            g_outputBuffer = "Error creating new server: " + string(e.what());
                            return std::shared_ptr<DownstreamState>();
                          }
 
                          if (IsAnyAddress(config.remote)) {
                            g_outputBuffer = "Error creating new server: invalid address for a downstream server.";
                            return std::shared_ptr<DownstreamState>();
                          }
 
                          LuaArray<std::string> pools;
                          if (getOptionalValue<std::string>(vars, "pool", valueStr, false) > 0) {
                            config.pools.insert(valueStr);
                          }
                          else if (getOptionalValue<decltype(pools)>(vars, "pool", pools) > 0) {
                            for (auto& pool : pools) {
                              config.pools.insert(pool.second);
                            }
                          }
 
                          auto ret = std::make_shared<DownstreamState>(std::move(config), std::move(tlsCtx), !(client || configCheck));
 
                          if (!ret->d_config.pools.empty()) {
                            for (const auto& poolName : ret->d_config.pools) {
                              addServerToPool(poolName, ret);
                            }
                          }
                          else {
                            addServerToPool("", ret);
                          }
 
                          if (ret->connected) {
                            if (dnsdist::configuration::isImmutableConfigurationDone()) {
                              ret->start();
                            }
                          }
 
                          dnsdist::backend::registerNewBackend(ret);
 
                          checkAllParametersConsumed("newServer", vars);
                          return ret;
                        });
 
   luaCtx.writeFunction("rmServer",
                        [](boost::variant<std::shared_ptr<DownstreamState>, int, std::string> var) {
                          setLuaSideEffect();
                          shared_ptr<DownstreamState> server = nullptr;
                          if (auto* rem = boost::get<shared_ptr<DownstreamState>>(&var)) {
                            server = *rem;
                          }
                          else if (auto* str = boost::get<std::string>(&var)) {
                            const auto uuid = getUniqueID(*str);
                            for (const auto& state : dnsdist::configuration::getCurrentRuntimeConfiguration().d_backends) {
                              if (*state->d_config.id == uuid) {
                                server = state;
                              }
                            }
                          }
                          else {
                            int idx = boost::get<int>(var);
                            server = dnsdist::configuration::getCurrentRuntimeConfiguration().d_backends.at(idx);
                          }
                          if (!server) {
                            throw std::runtime_error("unable to locate the requested server");
                          }
                          for (const string& poolName : server->d_config.pools) {
                            removeServerFromPool(poolName, server);
                          }
 
                          try {
                            removeServerFromPool("", server);
                          }
                          catch (const std::out_of_range& exp) {
                          }
 
                          dnsdist::configuration::updateRuntimeConfiguration([&server](dnsdist::configuration::RuntimeConfiguration& config) {
                            config.d_backends.erase(std::remove(config.d_backends.begin(), config.d_backends.end(), server), config.d_backends.end());
                          });
 
                          server->stop();
                        });
 
   luaCtx.writeFunction("getVerbose", []() { return dnsdist::configuration::getCurrentRuntimeConfiguration().d_verbose; });
 
   luaCtx.writeFunction("addACL", [](const std::string& mask) {
     setLuaSideEffect();
     dnsdist::configuration::updateRuntimeConfiguration([&mask](dnsdist::configuration::RuntimeConfiguration& config) {
       config.d_ACL.addMask(mask);
     });
   });
 
   luaCtx.writeFunction("rmACL", [](const std::string& netmask) {
     setLuaSideEffect();
     dnsdist::configuration::updateRuntimeConfiguration([&netmask](dnsdist::configuration::RuntimeConfiguration& config) {
       config.d_ACL.deleteMask(netmask);
     });
   });
 
   luaCtx.writeFunction("setLocal", [client](const std::string& addr, boost::optional<localbind_t> vars) {
     setLuaSideEffect();
     if (client) {
       return;
     }
 
     if (!checkConfigurationTime("setLocal")) {
       return;
     }
 
     bool reusePort = false;
     int tcpFastOpenQueueSize = 0;
     int tcpListenQueueSize = 0;
     uint64_t maxInFlightQueriesPerConn = 0;
     uint64_t tcpMaxConcurrentConnections = 0;
     std::string interface;
     std::set<int> cpus;
     bool enableProxyProtocol = true;
 
     parseLocalBindVars(vars, reusePort, tcpFastOpenQueueSize, interface, cpus, tcpListenQueueSize, maxInFlightQueriesPerConn, tcpMaxConcurrentConnections, enableProxyProtocol);
 
     auto frontends = dnsdist::configuration::getImmutableConfiguration().d_frontends;
     try {
       ComboAddress loc(addr, 53);
       for (auto it = frontends.begin(); it != frontends.end();) {
         it = frontends.erase(it);
       }
 
       auto udpCS = std::make_shared<ClientState>(loc, false, reusePort, tcpFastOpenQueueSize, interface, cpus, enableProxyProtocol);
       auto tcpCS = std::make_shared<ClientState>(loc, true, reusePort, tcpFastOpenQueueSize, interface, cpus, enableProxyProtocol);
       if (tcpListenQueueSize > 0) {
         tcpCS->tcpListenQueueSize = tcpListenQueueSize;
       }
       if (maxInFlightQueriesPerConn > 0) {
         tcpCS->d_maxInFlightQueriesPerConn = maxInFlightQueriesPerConn;
       }
       if (tcpMaxConcurrentConnections > 0) {
         tcpCS->d_tcpConcurrentConnectionsLimit = tcpMaxConcurrentConnections;
       }
 
       frontends.push_back(std::move(udpCS));
       frontends.push_back(std::move(tcpCS));
 
       checkAllParametersConsumed("setLocal", vars);
       dnsdist::configuration::updateImmutableConfiguration([&frontends](dnsdist::configuration::ImmutableConfiguration& config) {
         config.d_frontends = std::move(frontends);
       });
     }
     catch (const std::exception& e) {
       g_outputBuffer = "Error: " + string(e.what()) + "\n";
     }
   });
 
   luaCtx.writeFunction("addLocal", [client](const std::string& addr, boost::optional<localbind_t> vars) {
     setLuaSideEffect();
     if (client) {
       return;
     }
 
     if (!checkConfigurationTime("addLocal")) {
       return;
     }
     bool reusePort = false;
     int tcpFastOpenQueueSize = 0;
     int tcpListenQueueSize = 0;
     uint64_t maxInFlightQueriesPerConn = 0;
     uint64_t tcpMaxConcurrentConnections = 0;
     std::string interface;
     std::set<int> cpus;
     bool enableProxyProtocol = true;
 
     parseLocalBindVars(vars, reusePort, tcpFastOpenQueueSize, interface, cpus, tcpListenQueueSize, maxInFlightQueriesPerConn, tcpMaxConcurrentConnections, enableProxyProtocol);
 
     try {
       ComboAddress loc(addr, 53);
       auto udpCS = std::make_shared<ClientState>(loc, false, reusePort, tcpFastOpenQueueSize, interface, cpus, enableProxyProtocol);
       auto tcpCS = std::make_shared<ClientState>(loc, true, reusePort, tcpFastOpenQueueSize, interface, cpus, enableProxyProtocol);
       if (tcpListenQueueSize > 0) {
         tcpCS->tcpListenQueueSize = tcpListenQueueSize;
       }
       if (maxInFlightQueriesPerConn > 0) {
         tcpCS->d_maxInFlightQueriesPerConn = maxInFlightQueriesPerConn;
       }
       if (tcpMaxConcurrentConnections > 0) {
         tcpCS->d_tcpConcurrentConnectionsLimit = tcpMaxConcurrentConnections;
       }
       dnsdist::configuration::updateImmutableConfiguration([&udpCS, &tcpCS](dnsdist::configuration::ImmutableConfiguration& config) {
         config.d_frontends.push_back(std::move(udpCS));
         config.d_frontends.push_back(std::move(tcpCS));
       });
 
       checkAllParametersConsumed("addLocal", vars);
     }
     catch (std::exception& e) {
       g_outputBuffer = "Error: " + string(e.what()) + "\n";
     }
   });
 
   luaCtx.writeFunction("setACL", [](LuaTypeOrArrayOf<std::string> inp) {
     setLuaSideEffect();
     NetmaskGroup nmg;
     if (auto* str = boost::get<string>(&inp)) {
       nmg.addMask(*str);
     }
     else {
       for (const auto& entry : boost::get<LuaArray<std::string>>(inp)) {
         nmg.addMask(entry.second);
       }
     }
     dnsdist::configuration::updateRuntimeConfiguration([&nmg](dnsdist::configuration::RuntimeConfiguration& config) {
       config.d_ACL = std::move(nmg);
     });
   });
 
   luaCtx.writeFunction("setACLFromFile", [](const std::string& file) {
     setLuaSideEffect();
     NetmaskGroup nmg;
 
     ifstream ifs(file);
     if (!ifs) {
       throw std::runtime_error("Could not open '" + file + "': " + stringerror());
     }
 
     string::size_type pos = 0;
     string line;
     while (getline(ifs, line)) {
       pos = line.find('#');
       if (pos != string::npos) {
         line.resize(pos);
       }
       boost::trim(line);
       if (line.empty()) {
         continue;
       }
 
       nmg.addMask(line);
     }
 
     dnsdist::configuration::updateRuntimeConfiguration([&nmg](dnsdist::configuration::RuntimeConfiguration& config) {
       config.d_ACL = std::move(nmg);
     });
   });
 
   luaCtx.writeFunction("showACL", []() {
     setLuaNoSideEffect();
     auto aclEntries = dnsdist::configuration::getCurrentRuntimeConfiguration().d_ACL.toStringVector();
 
     for (const auto& entry : aclEntries) {
       g_outputBuffer += entry + "\n";
     }
   });
 
   void doExitNicely(int exitCode = EXIT_SUCCESS);
 
   luaCtx.writeFunction("shutdown", []() {
     doExitNicely();
   });
 
   typedef LuaAssociativeTable<boost::variant<bool, std::string>> showserversopts_t;
 
   luaCtx.writeFunction("showServers", [](boost::optional<showserversopts_t> vars) {
     setLuaNoSideEffect();
     bool showUUIDs = false;
     getOptionalValue<bool>(vars, "showUUIDs", showUUIDs);
     checkAllParametersConsumed("showServers", vars);
 
     try {
       ostringstream ret;
       boost::format fmt;
 
       auto latFmt = boost::format("%5.1f");
       if (showUUIDs) {
         fmt = boost::format("%1$-3d %15$-36s %2$-20.20s %|62t|%3% %|107t|%4$5s %|88t|%5$7.1f %|103t|%6$7d %|106t|%7$10d %|115t|%8$10d %|117t|%9$10d %|123t|%10$7d %|128t|%11$5.1f %|146t|%12$5s %|152t|%16$5s %|158t|%13$11d %14%");
         ret << (fmt % "#" % "Name" % "Address" % "State" % "Qps" % "Qlim" % "Ord" % "Wt" % "Queries" % "Drops" % "Drate" % "Lat" % "Outstanding" % "Pools" % "UUID" % "TCP") << endl;
       }
       else {
         fmt = boost::format("%1$-3d %2$-20.20s %|25t|%3% %|70t|%4$5s %|51t|%5$7.1f %|66t|%6$7d %|69t|%7$10d %|78t|%8$10d %|80t|%9$10d %|86t|%10$7d %|91t|%11$5.1f %|109t|%12$5s %|115t|%15$5s %|121t|%13$11d %14%");
         ret << (fmt % "#" % "Name" % "Address" % "State" % "Qps" % "Qlim" % "Ord" % "Wt" % "Queries" % "Drops" % "Drate" % "Lat" % "Outstanding" % "Pools" % "TCP") << endl;
       }
 
       uint64_t totQPS{0};
       uint64_t totQueries{0};
       uint64_t totDrops{0};
       int counter = 0;
       for (const auto& backend : dnsdist::configuration::getCurrentRuntimeConfiguration().d_backends) {
         string status = backend->getStatus();
         string pools;
         for (const auto& pool : backend->d_config.pools) {
           if (!pools.empty()) {
             pools += " ";
           }
           pools += pool;
         }
         const std::string latency = (backend->latencyUsec == 0.0 ? "-" : boost::str(latFmt % (backend->latencyUsec / 1000.0)));
         const std::string latencytcp = (backend->latencyUsecTCP == 0.0 ? "-" : boost::str(latFmt % (backend->latencyUsecTCP / 1000.0)));
         if (showUUIDs) {
           ret << (fmt % counter % backend->getName() % backend->d_config.remote.toStringWithPort() % status % backend->queryLoad % backend->getQPSLimit() % backend->d_config.order % backend->d_config.d_weight % backend->queries.load() % backend->reuseds.load() % (backend->dropRate) % latency % backend->outstanding.load() % pools % *backend->d_config.id % latencytcp) << endl;
         }
         else {
           ret << (fmt % counter % backend->getName() % backend->d_config.remote.toStringWithPort() % status % backend->queryLoad % backend->getQPSLimit() % backend->d_config.order % backend->d_config.d_weight % backend->queries.load() % backend->reuseds.load() % (backend->dropRate) % latency % backend->outstanding.load() % pools % latencytcp) << endl;
         }
         totQPS += static_cast<uint64_t>(backend->queryLoad);
         totQueries += backend->queries.load();
         totDrops += backend->reuseds.load();
         ++counter;
       }
       if (showUUIDs) {
         ret << (fmt % "All" % "" % "" % ""
                 % (double)totQPS % "" % "" % "" % totQueries % totDrops % "" % "" % "" % "" % "" % "")
             << endl;
       }
       else {
         ret << (fmt % "All" % "" % "" % ""
                 % (double)totQPS % "" % "" % "" % totQueries % totDrops % "" % "" % "" % "" % "")
             << endl;
       }
 
       g_outputBuffer = ret.str();
     }
     catch (std::exception& e) {
       g_outputBuffer = e.what();
       throw;
     }
   });
 
   luaCtx.writeFunction("getServers", []() {
     setLuaNoSideEffect();
     LuaArray<std::shared_ptr<DownstreamState>> ret;
     int count = 1;
     for (const auto& backend : dnsdist::configuration::getCurrentRuntimeConfiguration().d_backends) {
       ret.emplace_back(count++, backend);
     }
     return ret;
   });
 
   luaCtx.writeFunction("getPoolServers", [](const string& pool) {
     return getDownstreamCandidates(pool);
   });
 
   luaCtx.writeFunction("getServer", [client](boost::variant<unsigned int, std::string> identifier) -> boost::optional<std::shared_ptr<DownstreamState>> {
     if (client) {
       return std::make_shared<DownstreamState>(ComboAddress());
     }
     const auto& states = dnsdist::configuration::getCurrentRuntimeConfiguration().d_backends;
     if (auto* str = boost::get<std::string>(&identifier)) {
       const auto uuid = getUniqueID(*str);
       for (auto& state : states) {
         if (*state->d_config.id == uuid) {
           return state;
         }
       }
     }
     else if (auto* pos = boost::get<unsigned int>(&identifier)) {
       if (*pos < states.size()) {
         return states.at(*pos);
       }
       g_outputBuffer = "Error: trying to retrieve server " + std::to_string(*pos) + " while there is only " + std::to_string(states.size()) + "servers\n";
       return boost::none;
     }
 
     g_outputBuffer = "Error: no server matched\n";
     return boost::none;
   });
 
   // Query Counting Functions
   luaCtx.writeFunction("clearQueryCounters", []() {
     dnsdist::QueryCount::clear();
     g_outputBuffer = "Query counters cleared\n";
   });
 
   luaCtx.writeFunction("getQueryCounters", [](boost::optional<uint64_t> optMax) {
     setLuaNoSideEffect();
     auto records = dnsdist::QueryCount::getRecords(optMax ? *optMax : 10);
     g_outputBuffer = "query counting is currently: ";
     g_outputBuffer += dnsdist::QueryCount::getConfiguration().d_enabled ? "enabled" : "disabled";
     g_outputBuffer += (boost::format(" (%d records in buffer)\n") % records.size()).str();
 
     boost::format fmt("%-3d %s: %d request(s)\n");
     uint64_t index{1};
     for (const auto& record : records) {
       g_outputBuffer += (fmt % index % record.key % record.count).str();
       ++index;
     }
   });
 
   luaCtx.writeFunction("setQueryCountFilter", [](dnsdist::QueryCount::Configuration::Filter func) {
     auto config = dnsdist::QueryCount::getConfiguration();
     config.d_filter = std::move(func);
     dnsdist::QueryCount::setConfiguration(config);
   });
 
   luaCtx.writeFunction("enableQueryCounting", [](bool enable) {
     auto config = dnsdist::QueryCount::getConfiguration();
     config.d_enabled = enable;
     dnsdist::QueryCount::setConfiguration(config);
     g_outputBuffer = (boost::format("Query counting %s\n") % (enable ? "enabled" : "disabled")).str();
   });
 
   luaCtx.writeFunction("setMaxQueryCountRecords", [](size_t maxRecords) {
     auto config = dnsdist::QueryCount::getConfiguration();
     config.d_maxRecords = maxRecords;
     dnsdist::QueryCount::setConfiguration(config);
     g_outputBuffer = (boost::format("Maximum query count records set to %d\n") % maxRecords).str();
   });
 
   luaCtx.writeFunction("showPools", []() {
     setLuaNoSideEffect();
     try {
       ostringstream ret;
       boost::format fmt("%1$-20.20s %|25t|%2$20s %|25t|%3$20s %|50t|%4%");
       ret << (fmt % "Name" % "Cache" % "ServerPolicy" % "Servers") << endl;
 
       const auto defaultPolicyName = dnsdist::configuration::getCurrentRuntimeConfiguration().d_lbPolicy->getName();
       const auto pools = dnsdist::configuration::getCurrentRuntimeConfiguration().d_pools;
       for (const auto& entry : pools) {
         const string& name = entry.first;
         const auto& pool = entry.second;
         string cache = "";
         string policy = defaultPolicyName;
         if (pool.policy != nullptr) {
           policy = pool.policy->getName();
         }
         string servers;
 
         for (const auto& server : pool.getServers()) {
           if (!servers.empty()) {
             servers += ", ";
           }
           if (!server.second->getName().empty()) {
             servers += server.second->getName();
             servers += " ";
           }
           servers += server.second->d_config.remote.toStringWithPort();
         }
 
         ret << (fmt % name % cache % policy % servers) << endl;
       }
       g_outputBuffer = ret.str();
     }
     catch (std::exception& e) {
       g_outputBuffer = e.what();
       throw;
     }
   });
 
   luaCtx.writeFunction("getPoolNames", []() {
     setLuaNoSideEffect();
     LuaArray<std::string> ret;
     int count = 1;
     const auto& pools = dnsdist::configuration::getCurrentRuntimeConfiguration().d_pools;
     for (const auto& entry : pools) {
       const string& name = entry.first;
       ret.emplace_back(count++, name);
     }
     return ret;
   });
 
   luaCtx.writeFunction("getPool", [client](const string& poolName) {
     if (client) {
       return std::make_shared<dnsdist::lua::LuaServerPoolObject>(poolName);
     }
     bool created = false;
     dnsdist::configuration::updateRuntimeConfiguration([&poolName, &created](dnsdist::configuration::RuntimeConfiguration& config) {
       auto [_, inserted] = config.d_pools.emplace(poolName, ServerPool());
       created = inserted;
     });
 
     return std::make_shared<dnsdist::lua::LuaServerPoolObject>(poolName);
   });
 
   luaCtx.writeFunction("showBinds", []() {
     setLuaNoSideEffect();
     try {
       ostringstream ret;
       boost::format fmt("%1$-3d %2$-20.20s %|35t|%3$-20.20s %|57t|%4%");
       ret << (fmt % "#" % "Address" % "Protocol" % "Queries") << endl;
 
       size_t counter = 0;
       for (const auto& front : dnsdist::getFrontends()) {
         ret << (fmt % counter % front->local.toStringWithPort() % front->getType() % front->queries) << endl;
         counter++;
       }
       g_outputBuffer = ret.str();
     }
     catch (std::exception& e) {
       g_outputBuffer = e.what();
       throw;
     }
   });
 
   luaCtx.writeFunction("getBind", [](uint64_t num) {
     setLuaNoSideEffect();
     boost::optional<ClientState*> ret{boost::none};
     auto frontends = dnsdist::getFrontends();
     if (num < frontends.size()) {
       ret = frontends[num].get();
     }
     return ret;
   });
 
   luaCtx.writeFunction("getBindCount", []() {
     setLuaNoSideEffect();
     return dnsdist::getFrontends().size();
   });
 
   luaCtx.writeFunction("help", [](boost::optional<std::string> command) {
     setLuaNoSideEffect();
     g_outputBuffer = "";
     if (command) {
       g_outputBuffer = "Nothing found for " + *command + "\n";
     }
   });
 
   luaCtx.writeFunction("showVersion", []() {
     setLuaNoSideEffect();
     g_outputBuffer = "dnsdist " + std::string(VERSION) + "\n";
   });
 
   luaCtx.writeFunction("includeDirectory", [&luaCtx](const std::string& dirname) {
     if (!checkConfigurationTime("includeDirectory")) {
       return;
     }
     static bool s_included{false};
 
     if (s_included) {
       g_outputBuffer = "includeDirectory() cannot be used recursively!\n";
       return;
     }
 
     struct stat dirStat{};
     if (stat(dirname.c_str(), &dirStat) != 0) {
       g_outputBuffer = "The included directory " + dirname + " does not exist!";
       return;
     }
 
     if (!S_ISDIR(dirStat.st_mode)) {
       g_outputBuffer = "The included directory " + dirname + " is not a directory!";
       return;
     }
 
     std::vector<std::string> files;
     auto directoryError = pdns::visit_directory(dirname, [&dirname, &files]([[maybe_unused]] ino_t inodeNumber, const std::string_view& name) {
       if (boost::starts_with(name, ".")) {
         return true;
       }
       if (boost::ends_with(name, ".conf")) {
         std::ostringstream namebuf;
         namebuf << dirname << "/" << name;
         struct stat fileStat{};
         if (stat(namebuf.str().c_str(), &fileStat) == 0 && S_ISREG(fileStat.st_mode)) {
           files.push_back(namebuf.str());
         }
       }
       return true;
     });
 
     if (directoryError) {
       g_outputBuffer = "Error opening included directory: " + *directoryError + "!";
       return;
     }
 
     std::sort(files.begin(), files.end());
 
     s_included = true;
 
     for (const auto& file : files) {
       std::ifstream ifs(file);
       if (!ifs) {
       }
       else {
       }
 
       try {
         luaCtx.executeCode(ifs);
       }
       catch (...) {
         s_included = false;
         throw;
       }
 
       luaCtx.executeCode(ifs);
     }
 
     s_included = false;
   });
 
   luaCtx.writeFunction("setRingBuffersSize", [client](uint64_t capacity, boost::optional<uint64_t> numberOfShards) {
     if (client) {
       return;
     }
     setLuaSideEffect();
     try {
       dnsdist::configuration::updateImmutableConfiguration([capacity, numberOfShards](dnsdist::configuration::ImmutableConfiguration& config) {
         config.d_ringsCapacity = capacity;
         if (numberOfShards) {
           config.d_ringsNumberOfShards = *numberOfShards;
         }
       });
     }
     catch (const std::exception& exp) {
       g_outputBuffer = "setRingBuffersSize cannot be used at runtime!\n";
     }
   });
 
   luaCtx.writeFunction("setRingBuffersOptions", [client](const LuaAssociativeTable<boost::variant<bool, uint64_t>>& options) {
     if (client) {
       return;
     }
     setLuaSideEffect();
     try {
       dnsdist::configuration::updateImmutableConfiguration([&options](dnsdist::configuration::ImmutableConfiguration& config) {
         if (options.count("lockRetries") > 0) {
           config.d_ringsNbLockTries = boost::get<uint64_t>(options.at("lockRetries"));
         }
         if (options.count("recordQueries") > 0) {
           config.d_ringsRecordQueries = boost::get<bool>(options.at("recordQueries"));
         }
         if (options.count("recordResponses") > 0) {
           config.d_ringsRecordResponses = boost::get<bool>(options.at("recordResponses"));
         }
       });
     }
     catch (const std::exception& exp) {
       g_outputBuffer = "setRingBuffersOption cannot be used at runtime!\n";
     }
   });
 
   luaCtx.writeFunction("setTCPFastOpenKey", [](const std::string& keyString) {
     std::vector<uint32_t> key(4);
     auto ret = sscanf(keyString.c_str(), "%" SCNx32 "-%" SCNx32 "-%" SCNx32 "-%" SCNx32, &key.at(0), &key.at(1), &key.at(2), &key.at(3));
     if (ret < 0 || static_cast<size_t>(ret) != key.size()) {
       g_outputBuffer = "Invalid value passed to setTCPFastOpenKey()!\n";
       return;
     }
     dnsdist::configuration::updateImmutableConfiguration([&key](dnsdist::configuration::ImmutableConfiguration& config) {
       config.d_tcpFastOpenKey = std::move(key);
     });
   });
 
 #ifndef DISABLE_POLICIES_BINDINGS
   luaCtx.writeFunction("setServerPolicy", [](const std::shared_ptr<ServerPolicy>& policy) {
     setLuaSideEffect();
     dnsdist::configuration::updateRuntimeConfiguration([&policy](dnsdist::configuration::RuntimeConfiguration& config) {
       config.d_lbPolicy = policy;
     });
   });
 
   luaCtx.writeFunction("setServerPolicyLua", [](const string& name, ServerPolicy::policyfunc_t policy) {
     setLuaSideEffect();
     auto pol = std::make_shared<ServerPolicy>(name, std::move(policy), true);
     dnsdist::configuration::updateRuntimeConfiguration([&pol](dnsdist::configuration::RuntimeConfiguration& config) {
       config.d_lbPolicy = std::move(pol);
     });
   });
 
   luaCtx.writeFunction("showServerPolicy", []() {
     setLuaSideEffect();
     g_outputBuffer = dnsdist::configuration::getCurrentRuntimeConfiguration().d_lbPolicy->getName() + "\n";
   });
 
   luaCtx.writeFunction("setPoolServerPolicy", [](const std::shared_ptr<ServerPolicy>& policy, const string& pool) {
     setLuaSideEffect();
     setPoolPolicy(pool, policy);
   });
 
   luaCtx.writeFunction("setPoolServerPolicyLua", [](const string& name, ServerPolicy::policyfunc_t policy, const string& pool) {
     setLuaSideEffect();
     setPoolPolicy(pool, std::make_shared<ServerPolicy>(ServerPolicy{name, std::move(policy), true}));
   });
 
   luaCtx.writeFunction("showPoolServerPolicy", [](const std::string& pool) {
     setLuaSideEffect();
     const auto& poolObj = getPool(pool);
     if (poolObj.policy == nullptr) {
       g_outputBuffer = dnsdist::configuration::getCurrentRuntimeConfiguration().d_lbPolicy->getName() + "\n";
     }
     else {
       g_outputBuffer = poolObj.policy->getName() + "\n";
     }
   });
 #endif /* DISABLE_POLICIES_BINDINGS */
 
   luaCtx.writeFunction("setProxyProtocolACL", [](LuaTypeOrArrayOf<std::string> inp) {
     setLuaSideEffect();
     NetmaskGroup nmg;
     if (auto* str = boost::get<string>(&inp)) {
       nmg.addMask(*str);
     }
     else {
       for (const auto& entry : boost::get<LuaArray<std::string>>(inp)) {
         nmg.addMask(entry.second);
       }
     }
     dnsdist::configuration::updateRuntimeConfiguration([&nmg](dnsdist::configuration::RuntimeConfiguration& config) {
       config.d_proxyProtocolACL = std::move(nmg);
     });
   });
 
   luaCtx.writeFunction("setSyslogFacility", [](boost::variant<int, std::string> facility) {
     if (!checkConfigurationTime("setSyslogFacility")) {
       return;
     }
     setLuaSideEffect();
     if (facility.type() == typeid(std::string)) {
       const auto& facilityStr = boost::get<std::string>(facility);
       auto facilityLevel = logFacilityFromString(facilityStr);
       if (!facilityLevel) {
         g_outputBuffer = "Unknown facility '" + facilityStr + "' passed to setSyslogFacility()!\n";
         return;
       }
       setSyslogFacility(*facilityLevel);
     }
     else {
       setSyslogFacility(boost::get<int>(facility));
     }
   });
 
   typedef std::unordered_map<std::string, std::string> tlscertificateopts_t;
   luaCtx.writeFunction("newTLSCertificate", [client]([[maybe_unused]] const std::string& cert, [[maybe_unused]] boost::optional<tlscertificateopts_t> opts) {
     std::shared_ptr<TLSCertKeyPair> result = nullptr;
     if (client) {
       return result;
     }
 #if defined(HAVE_DNS_OVER_TLS) || defined(HAVE_DNS_OVER_HTTPS)
     std::optional<std::string> key;
     std::optional<std::string> password;
     if (opts) {
       if (opts->count("key") != 0) {
         key = boost::get<const string>((*opts)["key"]);
       }
       if (opts->count("password") != 0) {
         password = boost::get<const string>((*opts)["password"]);
       }
     }
     result = std::make_shared<TLSCertKeyPair>(cert, std::move(key), std::move(password));
 #endif
     return result;
   });
 }
 
 namespace dnsdist::lua
 {
 void setupLuaBindingsOnly(LuaContext& luaCtx, bool client, bool configCheck)
 {
   luaCtx.writeFunction("inClientStartup", [client]() {
     return client && !dnsdist::configuration::isImmutableConfigurationDone();
   });
 
   luaCtx.writeFunction("inConfigCheck", [configCheck]() {
     return configCheck;
   });
 
   luaCtx.writeFunction("enableLuaConfiguration", [&luaCtx, client, configCheck]() {
     setupLuaConfigurationOptions(luaCtx, client, configCheck);
   });
 
   setupLuaBindings(luaCtx, client, configCheck);
   setupLuaBindingsRings(luaCtx, client);
 }
 
 void setupLuaConfigurationOptions(LuaContext& luaCtx, bool client, bool configCheck)
 {
   static std::atomic<bool> s_initialized{false};
   if (s_initialized.exchange(true)) {
     return;
   }
 
   setupLuaConfig(luaCtx, client, configCheck);
 }
 
 void setupLua(LuaContext& luaCtx, bool client, bool configCheck)
 {
   setupLuaBindingsOnly(luaCtx, client, configCheck);
   setupLuaConfigurationOptions(luaCtx, client, configCheck);
 }
 }
 
 namespace dnsdist::configuration::lua
 {
 void loadLuaConfigurationFile(LuaContext& luaCtx, const std::string& config, bool configCheck)
 {
   std::ifstream ifs(config);
   if (!ifs) {
     if (configCheck) {
       throw std::runtime_error("Unable to read configuration file from " + config);
     }
   }
   else {
   }
 
   luaCtx.executeCode(ifs);
 }
 }