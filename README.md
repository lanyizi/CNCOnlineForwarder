# C&C:Online Forwarder
[![https://ci.appveyor.com/project/BSG-75/CNCOnlineForwarder](https://ci.appveyor.com/api/projects/status/github/BSG-75/CNCOnlineForwarder?svg=true)](https://ci.appveyor.com/project/BSG-75/CNCOnlineForwarder)

An unofficial proxy server for [C&amp;C:Online](https://cnc-online.net).

May help RA3 Players with connection issues, especially those caused by a [symmetric NAT](https://en.wikipedia.org/wiki/Network_address_translation) router.

**Note:** This proxy server cannot be used replace C&C:Online. You must already have your game started using [C&C:Online Client](https://cnc-online.net/download/) in order to use it.

Current features:
- [x] NatNeg Server Proxy: Help players to connect to each other by establishing relays between players.

Planned features:
- [ ] A client program which injects DLL into Red Alert 3 to enable features of CNCOnlineForwarder
- [ ] Peerchat Proxy: avoid TCP 6667 port's issues
- [ ] Local HTTP server: avoid the problem of _"Failed to connect to servers. Please check to make sure you have an active connection to the Internet"_ during log in of C&C:Online caused by high latency between http.server.cnc-online.net and player's computer.

# How to run this server
You can download built binaries from (AppVeyor)[https://ci.appveyor.com/project/BSG-75/CNCOnlineForwarder]. To run the server, make sure to allow this program in your Firewall Settings, since it will need to receive inbound UDP packets before sending them out. 

Currently only Windows Version is available (I'm developing it with Visual Studio). 
_But_ since the server part only uses C++ Standard Library and Boost, you should be able to port it to other platforms as well. If you can bring here some CMake build files, that would be really nice!
