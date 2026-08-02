#pragma once

#include "nl-socket.hpp"

namespace netlink {

// A stub NETLINK_NETFILTER (nfnetlink) protocol. managarm implements no netfilter/
// nftables backend, but systemd's firewall code opens this socket unconditionally and
// loops retrying socket() when it fails -- which wedges shutdown (DEF-79). Accepting the
// socket and answering every request that sets NLM_F_ACK with NLMSG_ERROR(-EOPNOTSUPP)
// lets that code observe "nftables unavailable" and fall back gracefully, exactly as on
// a Linux kernel built without nf_tables.
struct nfnetlink {
	static async::result<protocols::fs::Error> sendMsg(nl_socket::OpenFile *f, core::netlink::Packet packet, struct sockaddr_nl *sa);

	constexpr static struct nl_socket::ops ops{
		.sendMsg = sendMsg,
	};
};

} // namespace netlink
