/*
 * rcdiscover - the network discovery tool for Roboception devices
 *
 * Copyright (c) 2026 Roboception GmbH
 * All rights reserved
 *
 * Author: Heiko Hirschmueller
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "gvcp_ip_config.h"

#include "gige_request_counter.h"

#ifdef WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <cstring>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace rcdiscover
{

namespace
{

constexpr std::uint16_t READREG_CMD = 0x0080;
constexpr std::uint16_t READREG_ACK = 0x0081;
constexpr std::uint16_t WRITEREG_CMD = 0x0082;
constexpr std::uint16_t WRITEREG_ACK = 0x0083;

constexpr std::uint16_t GEV_STATUS_ACCESS_DENIED = 0x8006;

// GigE Vision bootstrap registers
constexpr std::uint32_t GEV_CCP = 0x00000A00;
constexpr std::uint32_t GEV_CURRENT_IP_CONFIGURATION = 0x00000014;
// Each field occupies a 16-byte-padded slot, matching the spacing of the
// already-confirmed current IP/subnet/gateway registers (0x24/0x34/0x44,
// see DeviceInfo::set()).
constexpr std::uint32_t GEV_PERSISTENT_IP_ADDRESS = 0x0000064C;
constexpr std::uint32_t GEV_PERSISTENT_SUBNET_MASK = 0x0000065C;
constexpr std::uint32_t GEV_PERSISTENT_DEFAULT_GATEWAY = 0x0000066C;

// Bits of GevCurrentIPConfiguration. The GigE Vision Standard numbers
// register bits MSB-first (its "bit 0" is the most significant bit), the
// reverse of the usual C/C++ convention where bit 0 is the LSB. The spec
// calls these bit 31 (Persistent IP), bit 30 (DHCP) and bit 29 (LLA); in
// standard bit-shift terms (bit = 31 - spec_bit) that is bit 0, bit 1 and
// bit 2 respectively.
constexpr std::uint32_t IPCONFIG_PERSISTENT_IP_BIT = 1u << 0;
constexpr std::uint32_t IPCONFIG_DHCP_BIT          = 1u << 1;
constexpr std::uint32_t IPCONFIG_LLA_BIT           = 1u << 2;

void appendUint32(std::vector<std::uint8_t> &v, const std::uint32_t value)
{
  v.push_back(static_cast<std::uint8_t>(value >> 24));
  v.push_back(static_cast<std::uint8_t>(value >> 16));
  v.push_back(static_cast<std::uint8_t>(value >> 8));
  v.push_back(static_cast<std::uint8_t>(value));
}

std::uint32_t extractUint32(const std::uint8_t *p)
{
  return (static_cast<std::uint32_t>(p[0]) << 24) |
        (static_cast<std::uint32_t>(p[1]) << 16) |
        (static_cast<std::uint32_t>(p[2]) << 8) |
        static_cast<std::uint32_t>(p[3]);
}

}

GvcpIPConfig::GvcpIPConfig(const std::uint32_t ip) :
  socket_(SocketType::create(htonl(ip), 3956, ""))
{
  socket_.enableNonBlocking();
}

std::vector<std::uint8_t> GvcpIPConfig::sendAndReceive(
    const std::uint16_t cmd, const std::uint16_t expected_ack_cmd,
    const std::uint32_t address, const std::vector<std::uint8_t> &payload)
{
  std::vector<std::uint8_t> packet(8);
  packet[0] = 0x42;
  packet[1] = 0x01; // request acknowledge
  packet[2] = static_cast<std::uint8_t>(cmd >> 8);
  packet[3] = static_cast<std::uint8_t>(cmd);
  packet[4] = static_cast<std::uint8_t>(payload.size() >> 8);
  packet[5] = static_cast<std::uint8_t>(payload.size());

  std::uint8_t req_id_hi, req_id_lo;
  std::tie(req_id_hi, req_id_lo) = GigERequestCounter::getNext();
  packet[6] = req_id_hi;
  packet[7] = req_id_lo;

  packet.insert(packet.end(), payload.begin(), payload.end());

  auto sock = socket_.getHandle<typename SocketType::SocketType>();

  for (int attempt = 0; attempt < 3; ++attempt)
  {
    socket_.send(packet);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);

    if (select(static_cast<int>(sock + 1), &fds, NULL, NULL, &tv) > 0)
    {
      std::uint8_t p[600];

      struct sockaddr_in addr;
#ifdef WIN32
      int naddr = sizeof(addr);
#else
      socklen_t naddr = sizeof(addr);
#endif
      std::memset(&addr, 0, sizeof(addr));

      long n = recvfrom(sock,
                        reinterpret_cast<char *>(p), sizeof(p), 0,
                        reinterpret_cast<struct sockaddr *>(&addr), &naddr);

      if (n >= 8)
      {
        const std::size_t len = (static_cast<std::size_t>(p[4]) << 8) | p[5];
        const std::uint16_t ack_cmd =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[2]) << 8) | p[3]);

        if (ack_cmd == expected_ack_cmd && p[6] == req_id_hi && p[7] == req_id_lo)
        {
          const std::uint16_t status =
              static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);

          if (status != 0)
          {
            std::ostringstream msg;
            msg << "Device returned error status 0x" << std::hex << status
                << " for GVCP command 0x" << cmd
                << " on register 0x" << address;

            if (status == GEV_STATUS_ACCESS_DENIED)
            {
              throw GvcpAccessDeniedException(msg.str());
            }

            throw std::runtime_error(msg.str());
          }

          if (static_cast<std::size_t>(n) >= len + 8)
          {
            return std::vector<std::uint8_t>(p + 8, p + 8 + len);
          }
        }
      }
    }
  }

  std::ostringstream msg;
  msg << "Device did not respond to GVCP command 0x" << std::hex << cmd
      << " on register 0x" << address;
  throw std::runtime_error(msg.str());
}

std::uint32_t GvcpIPConfig::readRegister(const std::uint32_t address)
{
  std::vector<std::uint8_t> payload;
  appendUint32(payload, address);

  const auto response = sendAndReceive(READREG_CMD, READREG_ACK, address, payload);

  if (response.size() < 4)
  {
    throw std::runtime_error("Invalid response to READREG command");
  }

  return extractUint32(response.data());
}

void GvcpIPConfig::writeRegister(const std::uint32_t address, const std::uint32_t value)
{
  std::vector<std::uint8_t> payload;
  appendUint32(payload, address);
  appendUint32(payload, value);

  sendAndReceive(WRITEREG_CMD, WRITEREG_ACK, address, payload);
}

IPConfig GvcpIPConfig::readConfig()
{
  IPConfig config{};

  const std::uint32_t mode = readRegister(GEV_CURRENT_IP_CONFIGURATION);

  config.persistent_ip_enabled = (mode & IPCONFIG_PERSISTENT_IP_BIT) != 0;
  config.dhcp_enabled = (mode & IPCONFIG_DHCP_BIT) != 0;

  config.persistent_ip = readRegister(GEV_PERSISTENT_IP_ADDRESS);
  config.persistent_subnet = readRegister(GEV_PERSISTENT_SUBNET_MASK);
  config.persistent_gateway = readRegister(GEV_PERSISTENT_DEFAULT_GATEWAY);

  return config;
}

void GvcpIPConfig::writeConfig(const IPConfig &config)
{
  // claim control access, required to write bootstrap registers
  writeRegister(GEV_CCP, 1);

  try
  {
    if (config.persistent_ip_enabled)
    {
      writeRegister(GEV_PERSISTENT_IP_ADDRESS, config.persistent_ip);
      writeRegister(GEV_PERSISTENT_SUBNET_MASK, config.persistent_subnet);
      writeRegister(GEV_PERSISTENT_DEFAULT_GATEWAY, config.persistent_gateway);
    }

    // read-modify-write so that any other (e.g. vendor-specific) bits in
    // this register are preserved
    std::uint32_t mode = readRegister(GEV_CURRENT_IP_CONFIGURATION);

    mode &= ~(IPCONFIG_PERSISTENT_IP_BIT | IPCONFIG_DHCP_BIT | IPCONFIG_LLA_BIT);

    if (config.persistent_ip_enabled) mode |= IPCONFIG_PERSISTENT_IP_BIT;
    if (config.dhcp_enabled) mode |= IPCONFIG_DHCP_BIT;
    mode |= IPCONFIG_LLA_BIT; // link-local is always the fallback, cannot be disabled

    writeRegister(GEV_CURRENT_IP_CONFIGURATION, mode);
  }
  catch (...)
  {
    try { writeRegister(GEV_CCP, 0); } catch (...) { }
    throw;
  }

  // release control access again (best effort)
  try { writeRegister(GEV_CCP, 0); } catch (...) { }
}

}
