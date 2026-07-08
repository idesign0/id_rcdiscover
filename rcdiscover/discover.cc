/*
 * rcdiscover - the network discovery tool for Roboception devices
 *
 * Copyright (c) 2017 Roboception GmbH
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

#include "discover.h"

#include "socket_exception.h"
#include "gige_request_counter.h"

#include <exception>
#include <ios>
#include <iostream>

#ifdef WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <vector>
#include <chrono>
#include <utility>
#include <string.h>
#include <errno.h>
#include <algorithm>

namespace rcdiscover
{

#ifdef WIN32
typedef SocketWindows SocketImpl;
#else
typedef SocketLinux SocketImpl;
#endif

Discover::Discover() :
  sockets_(SocketType::createAndBindForAllInterfaces(3956)),
  stop_(false)
{
  for (auto &socket : sockets_)
  {
    socket.enableBroadcast();
    socket.enableNonBlocking();
  }
}

Discover::~Discover()
{
  stop_=true;

  for (auto &t : listener_threads_)
  {
    if (t.joinable())
    {
      t.join();
    }
  }
}

void Discover::broadcastRequest()
{
  broadcastRequest(std::vector<std::string>());
}

void Discover::broadcastRequest(const std::vector<std::string> &iface)
{
  req_nums_.clear();

  std::vector<uint8_t> discovery_cmd{0x42, 0x11, 0, 0x02, 0, 0, 0, 0};

  for (auto &socket : sockets_)
  {
    if (iface.size() == 0 || std::find(iface.begin(), iface.end(), socket.getIfaceName()) != iface.end())
    {
      req_nums_.push_back(GigERequestCounter::getNext());
      std::tie(discovery_cmd[6], discovery_cmd[7]) = req_nums_.back();

      try
      {
        socket.send(discovery_cmd);
      }
      catch(const NetworkUnreachableException &)
      {
        continue;
      }
    }
  }

  // req_nums_ is now final for the lifetime of this object, so it is safe
  // for the listener threads to read it without further synchronization

  startListening();
}

void Discover::startListening()
{
  for (auto &socket : sockets_)
  {
    listener_threads_.emplace_back(&Discover::listenOnSocket, this, std::ref(socket));
  }
}

void Discover::listenOnSocket(SocketType &socket)
{
  auto sock = socket.getHandle<typename SocketType::SocketType>();

  while (!stop_)
  {
    // fd_set and timeout must be reinitialized on every iteration since
    // select() may modify both in place; a short timeout is used so
    // destruction of this object is not delayed for long

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);

    struct timeval tv;
    tv.tv_sec=0;
    tv.tv_usec=100000;

    if (select(static_cast<int>(sock+1), &fds, NULL, NULL, &tv) > 0)
    {
      // get package

      uint8_t p[600];

      struct sockaddr_in addr;
#ifdef WIN32
      int naddr = sizeof(addr);
#else
      socklen_t naddr = sizeof(addr);
#endif
      memset(&addr, 0, naddr);

      long n = recvfrom(sock,
                        reinterpret_cast<char *>(p), sizeof(p), 0,
                        reinterpret_cast<struct sockaddr *>(&addr), &naddr);

      // check if received package is a valid discovery acknowledge

      if (n >= 8)
      {
        if (p[0] == 0 && p[1] == 0 && p[2] == 0 &&
            p[3] == 0x03)
        {
          if (std::find(req_nums_.begin(), req_nums_.end(),
                        std::make_tuple(p[6], p[7])) != req_nums_.end())
          {
            size_t len=(static_cast<size_t>(p[4])<<8)|p[5];

            if (static_cast<size_t>(n) >= len+8)
            {
              // extract information and store in list

              DeviceInfo device_info(socket.getIfaceName());
              device_info.set(p+8, len);

              if (device_info.isValid())
              {
                {
                  std::lock_guard<std::mutex> lock(mutex_);
                  pending_.push_back(std::move(device_info));
                }

                cv_.notify_one();
              }
            }
          }
        }
      }
    }
  }
}

bool Discover::getResponse(std::vector<DeviceInfo> &info,
                           int timeout_per_socket)
{
  std::unique_lock<std::mutex> lock(mutex_);

  cv_.wait_for(lock, std::chrono::milliseconds(timeout_per_socket),
              [this] { return !pending_.empty(); });

  bool ret = !pending_.empty();

  for (auto &device_info : pending_)
  {
    info.push_back(std::move(device_info));
  }
  pending_.clear();

  return ret;
}

}
