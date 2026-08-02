#include <errno.h>
#include <linux/netlink.h>

#include "core/netlink.hpp"
#include "nfnetlink.hpp"

namespace netlink {

async::result<protocols::fs::Error> nfnetlink::sendMsg(nl_socket::OpenFile *f, core::netlink::Packet packet, struct sockaddr_nl *sa) {
	if(!sa->nl_pid)
		sa->nl_pid = f->socketPort();

	// We implement no nftables backend. Answer every message that requests an ack with
	// NLMSG_ERROR(-EOPNOTSUPP) -- the honest "nf_tables not available" a Linux kernel
	// built without it gives, so a client's blocking recv() returns instead of hanging on
	// an otherwise-empty socket, and its firewall stays genuinely off (an ack-with-success
	// would silently defeat any IP access control the caller believes it installed). A
	// batch (BEGIN..END) is answered message-by-message, like the kernel.
	auto nlh = reinterpret_cast<struct nlmsghdr *>(packet.buffer.data());
	// NLMSG_OK/NLMSG_NEXT must run over a signed length: trailing alignment padding can
	// drive it below zero, and only a signed compare terminates the walk correctly.
	int len = static_cast<int>(packet.buffer.size());
	bool answered = false;
	for(; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
		if(nlh->nlmsg_flags & NLM_F_ACK) {
			core::netlink::sendError(f, nlh, EOPNOTSUPP, sa);
			answered = true;
		}
	}

	// A dump/get without NLM_F_ACK still expects a reply; answer the first header so the
	// caller's recv() makes progress rather than blocking forever.
	if(!answered)
		core::netlink::sendError(f, reinterpret_cast<struct nlmsghdr *>(packet.buffer.data()), EOPNOTSUPP, sa);

	co_return protocols::fs::Error::none;
}

} // namespace netlink
