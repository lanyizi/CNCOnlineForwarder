# C&C:Online Forwarder
[![https://ci.appveyor.com/project/BSG-75/CNCOnlineForwarder](https://ci.appveyor.com/api/projects/status/github/BSG-75/CNCOnlineForwarder?svg=true)](https://ci.appveyor.com/project/BSG-75/CNCOnlineForwarder)

An unofficial proxy server for [C&amp;C:Online](https://cnc-online.net).

May help RA3 Players with NATNEG issues.

Current features:
- [x] NatNeg Server Proxy: Help players to connect to each other by establishing relays between players.

Planned features:
- [ ] A client program which injects DLL into Red Alert 3 to enable features of CNCOnlineForwarder
- [ ] Peerchat Proxy: avoid TCP 6667 port's issues
- [ ] Local HTTP server: avoid the problem of _"Failed to connect to servers. Please check to make sure you have an active connection to the Internet"_ during log in of C&C:Online caused by high latency between http.server.cnc-online.net and player's computer.