# Networking Notes

The project implements one thing for real: an **IPv4 TCP status server and
client**. Everything else on the syllabus is written up here, with the reason
it is not in the code.

---

## 1. The project's traffic, mapped to the models

```
+-----------+  +-------------+  +----------+  +-----------+  +-----------+  +----------+
|  Data     |  | Application |  |  Session |  | Transport |  |  Network  |  | Link/    |
|  (bytes)  |  |  HTTP-like  |  |  TCP     |  |   TCP     |  |   IPv4    |  | Physical |
+-----------+  +-------------+  +----------+  +-----------+  +-----------+  +----------+
"STATUS distance=37 state=WARNING\n"
```

| Layer | What the project does |
|---|---|
| 7 Application | the `STATUS ...` line; `client/status_client.cpp` prints it |
| 6 Presentation | nothing needed - ASCII needs no encoding |
| 5 Session | one request/response per connection, then close |
| 4 Transport | TCP: reliable, ordered, with the three-way handshake |
| 3 Network | IPv4; the loopback address 127.0.0.1, port 9000 |
| 2 Data link | loopback on the same host; Ethernet if the client is another machine |
| 1 Physical | a cable or Wi-Fi in a real deployment |

**TCP/IP model** groups these as: Application (7), Transport (4), Internet (3),
Network Access (2+1). The only structural difference from OSI is that OSI
separates Session and Presentation, which TCP/IP does not.

---

## 2. What actually happens on the wire

```
client                                                  server
------                                                  ------
socket(AF_INET, SOCK_STREAM, 0)
setsockopt(SO_REUSEADDR)
bind()      (the server does this)
listen()    (the server does this)
connect()  ---------------- SYN -------------------->   accept() in the accept thread
            <--------------- SYN-ACK -----------------
            ----------------- ACK ------------------->  accept() returns
                                              <------- send("STATUS ...")
                                              <------- shutdown(SHUT_WR)
                                              <------- close()
            <--------- data, then FIN, then FIN -------
recv() returns the status line
close()
```

Points to be ready to explain:

- `listen()` before `accept()` is not optional; the kernel completes handshakes
  in the backlog queue.
- The server's `send()` and the client's `recv()` may both succeed while the
  data is still in the network - TCP is a byte stream, not a message protocol.
  That is why the protocol here ends with `\n`: a stream needs a delimiter.
- `shutdown(SHUT_WR)` sends a FIN but keeps the read side open. The client uses
  it to say "I will send nothing more"; the server uses it to close its write
  side politely.

---

## 3. IPv4 versus IPv6

| | IPv4 | IPv6 |
|---|---|---|
| Address size | 32 bits | 128 bits |
| Text form | `192.168.1.10` | `2001:db8::1` |
| Header | 20-60 bytes, variable | fixed 40 bytes |
| Configuration | usually DHCP or manual | usually SLAAC (router advertisement) |
| Fragmentation | routers and hosts | source host only |
| Broadcast | yes | no, uses multicast |
| NAT | almost always needed | designed to remove the need |
| Address exhaustion | exhausted years ago | deployment still partial |

**What changes in this project to support IPv6.** Very little:

```c
struct sockaddr_in6 addr;
addr.sin6_family = AF_INET6;
addr.sin6_port   = htons(port);
addr.sin6_addr   = in6addr_loopback;      /* or :: */
socket(AF_INET6, SOCK_STREAM, 0);
```

plus, optionally, `setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, 0)` to accept IPv4
on the same socket. **What does not change:** the status message, the state
machine, the log format, and the client. That is the point of a layered design -
the address family is confined to two files.

---

## 4. Subnetting example for the demo network

The classful era is over; CIDR is what is used now.

```
Network:  192.168.10.0/24
  192.168.10.0     network address   (not assignable)
  192.168.10.1     default gateway
  192.168.10.10    monitor / status server   TCP 9000
  192.168.10.20    status client
  192.168.10.255   broadcast          (not assignable)
  usable hosts: 2^8 - 2 = 254
```

How the /24 is read: `192.168.10` is the network prefix (24 bits), the last
8 bits are host bits, so `2^8 = 256` addresses, minus network and broadcast.

Splitting it further:

| Prefix | Hosts per subnet | Network | Range |
|---|---|---|---|
| /24 | 254 | 192.168.10.0 | .0 - .255 |
| /25 | 126 | 192.168.10.0 | .0 - .127 |
| /26 | 62 | 192.168.10.64 | .64 - .127 |
| /30 | 2 | 192.168.10.8 | .8 - .11 (a point-to-point link) |

A /30 is the classic choice for a router-to-router link: four addresses, two
usable.

The demo runs on `127.0.0.0/8` (loopback), which is not routable at all - which
is exactly why it is the safest place to demonstrate the project.

---

## 5. NAT

**Network Address Translation** rewrites addresses at a router so many private
hosts can share one public address.

```
192.168.10.20:51000  --->  203.0.113.7:40001  --->  server
        private:port            public:port (NAT table)
```

The router keeps a table of `(private address, port) -> (public address, port)`
and replaces the source address on the way out, the destination on the way back.
Consequences worth stating:

- inbound connections have no table entry, so unsolicited packets are dropped -
  which is the security side effect that motivates firewalls;
- the port field is what makes many hosts share one address (PAT);
- a stateful firewall is essentially the same table with an accept/drop policy.

**In this project.** None of it. The monitor binds `127.0.0.1`, which is
loopback and never leaves the machine. A real deployment on a LAN would need
the port opened, and that is the one sentence the report should contain.

---

## 6. Firewall, VPN, and securing the endpoint

**Firewall** - filters packets by source/destination, port, protocol and
connection state.

```bash
sudo ufw allow 9000/tcp        # allow the status port
sudo ufw status
sudo iptables -L -n -v         # the packet-filter view
```

**VPN** - an encrypted tunnel over an untrusted network. The two families:

| | Site-to-site | Remote access |
|---|---|---|
| Tunnels many hosts as one network | yes | no |
| Typical | office to data centre | laptop to office |
| IPsec | common | yes |
| OpenVPN / WireGuard | yes | yes |

**Why the project uses plain TCP.** The status server publishes a distance in
centimetres and a state - not personal data and not credentials. TLS would add a
certificate, a handshake per connection and a dependency, for no confidentiality
benefit on a loopback demo. The correct engineering answer is: *do not expose
this port beyond the local machine; if you must, tunnel it or put TLS in front.*
That sentence is the whole of section 12 of the SRS, and it is a defensible
one.

---

## 7. Application-layer protocols from the syllabus

| Protocol | Port | Transport | What it does | In this project? |
|---|---|---|---|---|
| HTTP | 80 / 443 | TCP | request/response for web resources | no - the payload is one line, not a document |
| FTP | 20 / 21 | TCP | file transfer, two channels | no - the client only reads status |
| SMTP | 25 | TCP | sending mail | no - the monitor must not send anything |
| SNMP | 161 / 162 | UDP | agent polling for device metrics | **closest cousin**: an ultrasonic sensor really is usually an SNMP-managed device |
| SIP | 5060 | TCP/UDP | session setup for VoIP | no |
| DNS | 53 | UDP/TCP | name to address | only in a real deployment, not with `127.0.0.1` |
| DHCP | 67 / 68 | UDP | automatic address assignment | a lab machine's problem, not the project's |

The honest comparison: HTTP would let any browser or `curl` read the status, so
it would be a *better* demo - but it is not what the SRS specifies, and adding
a web server would mean parsing requests, which is scope the project
deliberately avoids. That trade-off is a good answer if you are asked why you
did not "just use HTTP".

---

## 8. Wired Ethernet versus Wi-Fi

| | Ethernet | Wi-Fi |
|---|---|---|
| Medium | twisted pair / fibre | radio, 2.4 and 5 GHz |
| Speed | 1 / 10 / 100 / 1000 Gb/s | 100 Mb/s to several Gb/s |
| Latency | microseconds, stable | milliseconds, variable |
| Duplex | full duplex always | shared medium, half duplex emulated |
| Security | physical + optional 802.1X | WPA2/WPA3 mandatory in practice |
| Interference | none | walls, other networks, radar |
| Cost per port | higher | almost free |

**Deployment scenarios for this project:**

- **Inside the vehicle (Ethernet/CAN):** a car network is a wired, deterministic
  bus. Deterministic latency is the deciding factor, and a plain socket over a
  wired link is the right shape.
- **Fleet / IoT gateway (Wi-Fi or cellular):** a parking sensor per bay,
  reporting over 5G or Wi-Fi. Here bandwidth is tiny - about 40 bytes per
  second - so the constraint is coverage and power, not throughput.
- **Loopback (this project):** neither, which is why latency is negligible and
  the demo is reliable.

The measured point: at one status line per poll, the bandwidth is about
`40 bytes x 1 per second` - a few hundred bits per second. **Networking is not
the bottleneck of this system; the state machine and the file descriptor are.**

---

## 9. 5G and IoT

A parking sensor is a canonical IoT node: tiny, always on, mostly idle, and the
interesting part is not the data but the alert.

| IoT characteristic | This project |
|---|---|
| constrained device | a Linux host, not an MCU - but the data rate is IoT-scale |
| low power | not applicable on a laptop; a real node would duty-cycle |
| many nodes | 4 bytes of state per bay instead of a video feed |
| unreliable link | the monitor counts read errors and keeps going (FR-16) |
| need an alert | DANGER is exactly the event worth notifying on |

**What 5G would change:** latency and connection setup time. For a parking
sensor neither matters at a 1 Hz sample rate. 5G matters for closed-loop
control - a vehicle braking on a sensor reading - where millisecond latency
does matter, and even then a wired CAN bus inside the vehicle is usually
preferred because it is more deterministic.

**The honest position:** this project's networking is deliberately the simplest
part. It exists to demonstrate that data leaves the process and to show the
socket path from the syllabus. Nothing about it needs 5G, and pretending
otherwise would be a worse answer than saying "this is 40 bytes a second on
loopback".
