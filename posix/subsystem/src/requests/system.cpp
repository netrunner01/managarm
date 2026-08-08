#include "common.hpp"
#include "../requests.hpp"
#include "../procfs.hpp"
#include "../device.hpp"
#include "../pts.hpp"
#include "../sysfs.hpp"
#include "../tmp_fs.hpp"
#include "../cgroupfs.hpp"
#include <unistd.h>
#include <sys/reboot.h>
#include <async/basic.hpp>
#include <helix/timer.hpp>
#include <kerncfg.bragi.hpp>
#include <hw.bragi.hpp>

namespace requests {

namespace {

bool shutdownWatchdogArmed = false;

// TEMPORARY WORKAROUND -- NOT FOR PRODUCTION. This papers over DEF-79 (systemd-shutdown
// wedging on unimplemented low-level operations in its final umount/detach phase) instead of
// fixing it: it forces a poweroff after a fixed grace period rather than letting shutdown
// complete on its own. The real fix is to implement the missing operations so
// reboot(RB_POWER_OFF) is reached normally; this should be removed once that lands. Retained
// only so a graceful `systemctl poweroff` actually powers the machine off during the desktop
// investigation.
//
// systemd-shutdown enables Ctrl-Alt-Del (reboot(RB_ENABLE_CAD)) right after "Shutting down.",
// immediately before its final umount/detach loop -- the phase that wedges. By that point all
// graceful userspace work (services stopped, filesystems unmounted per the shutdown targets)
// is already done, so if the normal reboot(RB_POWER_OFF) has not arrived within the grace
// period we force the poweroff ourselves through the same pm-interface path. This mirrors what
// CAD itself is for: a safety valve to force-terminate a hung shutdown.
async::result<void> shutdownWatchdog() {
	co_await helix::sleepFor(15'000'000'000); // 15 s grace
	std::cout << "posix: shutdown watchdog -- systemd-shutdown did not reach reboot(); forcing poweroff" << std::endl;

	// Let that line actually reach the console before we halt: the poweroff below is the last
	// thing the machine does, and an S5 that races it drops the final debug output, leaving an
	// unexplained poweroff in the log. A short delay guarantees the "why" is recorded.
	co_await helix::sleepFor(250'000'000); // 250 ms

	// Fire-and-forget: thor powers the machine off from a scheduled work item and sends no
	// reboot response, so we must not wait for one (waiting would race the poweroff).
	managarm::hw::RebootRequest hwRequest;
	hwRequest.set_cmd(RB_POWER_OFF);
	auto [offer, hwSendResp] = co_await helix_ng::exchangeMsgs(
		getPmLane(),
		helix_ng::offer(
			helix_ng::sendBragiHeadOnly(hwRequest, frg::stl_allocator{})
		)
	);
	HEL_CHECK(offer.error());
	HEL_CHECK(hwSendResp.error());
}

} // namespace

async::result<std::expected<void, DispatchError>>
HandleRequest::operator()(managarm::posix::RebootRequest &&req,
		helix::BorrowedDescriptor conversation, bragi::preamble preamble,
		std::shared_ptr<Process> self, std::shared_ptr<Generation>) {
	id = preamble.id();
	logBragiRequest(req);

	logRequest(logRequests, self, "REBOOT", "command={}", req.cmd());

	if(self->threadGroup()->uid() != 0) {
		co_await sendErrorResponse<managarm::posix::RebootResponse>(conversation, managarm::posix::Errors::INSUFFICIENT_PERMISSION);
		co_return {};
	}

	// thor has no Ctrl-Alt-Del to toggle; don't forward CAD commands to it. RB_ENABLE_CAD,
	// which mlibc forwards here because systemd-shutdown issues it right before its
	// (wedge-prone) final phase, arms the poweroff watchdog. Both CAD toggles answer success,
	// as they do on Linux.
	// Compare the low 32 bits: the wire field is int64 but reboot(2) commands are 32-bit, and
	// RB_ENABLE_CAD (0x89abcdef) sign-extends to a negative int64 that would not match the
	// unsigned macro directly.
	auto rebootCmd = static_cast<uint32_t>(req.cmd());
	if(rebootCmd == RB_ENABLE_CAD || rebootCmd == RB_DISABLE_CAD) {
		if(rebootCmd == RB_ENABLE_CAD && !shutdownWatchdogArmed) {
			shutdownWatchdogArmed = true;
			async::detach(shutdownWatchdog());
		}

		managarm::posix::RebootResponse resp;
		resp.set_error(managarm::posix::Errors::SUCCESS);
		auto [send_resp] = co_await helix_ng::exchangeMsgs(conversation,
			helix_ng::sendBragiHeadOnly(resp, frg::stl_allocator{})
		);
		HEL_CHECK(send_resp.error());
		logBragiReply(resp);
		co_return {};
	}

	managarm::hw::RebootRequest hwRequest;
	hwRequest.set_cmd(req.cmd());
	auto [offer, hwSendResp, hwResp] = co_await helix_ng::exchangeMsgs(
		getPmLane(),
		helix_ng::offer(
			helix_ng::sendBragiHeadOnly(hwRequest, frg::stl_allocator{}),
			helix_ng::recvInline()
		)
	);
	HEL_CHECK(offer.error());
	HEL_CHECK(hwSendResp.error());
	HEL_CHECK(hwResp.error());
	hwResp.reset();

	managarm::posix::RebootResponse resp;

	resp.set_error(managarm::posix::Errors::SUCCESS);

	auto [send_resp] = co_await helix_ng::exchangeMsgs(conversation,
		helix_ng::sendBragiHeadOnly(resp, frg::stl_allocator{})
	);
	HEL_CHECK(send_resp.error());
	logBragiReply(resp);
	co_return {};
}

async::result<std::expected<void, DispatchError>>
HandleRequest::operator()(managarm::posix::MountRequest &&req,
		helix::BorrowedDescriptor conversation, bragi::preamble preamble,
		std::shared_ptr<Process> self, std::shared_ptr<Generation>) {
	id = preamble.id();

	auto tailRes = co_await dispatchTail(req, conversation, preamble);
	if(!tailRes)
		co_return std::unexpected(tailRes.error());
	logBragiRequest(req);

	logRequest(logRequests, self, "MOUNT", "fstype={} on={} to={}", req.fs_type(), req.path(), req.target_path());

	if(self->threadGroup()->uid() != 0) {
		co_await sendErrorResponse<managarm::posix::MountResponse>(conversation, managarm::posix::Errors::INSUFFICIENT_PERMISSION);
		co_return {};
	}

	auto resolveResult = co_await resolve(self->fsContext()->getRoot(),
			self->fsContext()->getWorkingDirectory(), req.target_path(), self.get());
	if(!resolveResult) {
		if(resolveResult.error() == protocols::fs::Error::fileNotFound) {
			co_await sendErrorResponse<managarm::posix::MountResponse>(conversation, managarm::posix::Errors::FILE_NOT_FOUND);
			co_return {};
		} else if(resolveResult.error() == protocols::fs::Error::notDirectory) {
			co_await sendErrorResponse<managarm::posix::MountResponse>(conversation, managarm::posix::Errors::NOT_A_DIRECTORY);
			co_return {};
		} else {
			std::cout << "posix: Unexpected failure from resolve()" << std::endl;
			co_return {};
		}
	}
	auto target = resolveResult.value();

	if(req.fs_type() == "procfs" || req.fs_type() == "proc") {
		co_await target.first->mount(target.second, getProcfs());
	}else if(req.fs_type() == "sysfs") {
		co_await target.first->mount(target.second, getSysfs());
	}else if(req.fs_type() == "devtmpfs") {
		co_await target.first->mount(target.second, getDevtmpfs());
	}else if(req.fs_type() == "tmpfs") {
		auto res = tmp_fs::createRoot(self.get(), req.mount_data());
		if (!res) {
			co_await sendErrorResponse<managarm::posix::MountResponse>(conversation, res.error() | toPosixProtoError);
			co_return {};
		}
		co_await target.first->mount(target.second, *res);
	}else if(req.fs_type() == "devpts") {
		co_await target.first->mount(target.second, pts::getFsRoot());
	}else if(req.fs_type() == "cgroup2") {
		co_await target.first->mount(target.second, getCgroupfs());
	}else{
		if(req.fs_type() != "ext2" && req.fs_type() != "btrfs") {
			std::cout << "posix: Trying to mount unsupported FS of type: " << req.fs_type() << std::endl;
			co_await sendErrorResponse<managarm::posix::MountResponse>(conversation, managarm::posix::Errors::NO_BACKING_DEVICE);
			co_return {};
		}

		auto sourceResult = co_await resolve(self->fsContext()->getRoot(),
				self->fsContext()->getWorkingDirectory(), req.path(), self.get());
		if(!sourceResult) {
			if(sourceResult.error() == protocols::fs::Error::fileNotFound) {
				co_await sendErrorResponse<managarm::posix::MountResponse>(conversation, managarm::posix::Errors::FILE_NOT_FOUND);
				co_return {};
			} else if(sourceResult.error() == protocols::fs::Error::notDirectory) {
				co_await sendErrorResponse<managarm::posix::MountResponse>(conversation, managarm::posix::Errors::NOT_A_DIRECTORY);
				co_return {};
			} else {
				std::cout << "posix: Unexpected failure from resolve()" << std::endl;
				co_return {};
			}
		}
		auto source = sourceResult.value();
		assert(source.second);
		assert(source.second->getTarget()->getType() == VfsType::blockDevice);
		auto device = blockRegistry.get(source.second->getTarget()->readDevice());
		auto link = co_await device->mount(req.fs_type());
		co_await target.first->mount(target.second, std::move(link), source);
	}

	logRequest(logRequests, self, "MOUNT", "succeeded");

	managarm::posix::MountResponse resp;
	resp.set_error(managarm::posix::Errors::SUCCESS);

	auto [send_resp] = co_await helix_ng::exchangeMsgs(
				conversation,
				helix_ng::sendBragiHeadOnly(resp, frg::stl_allocator{})
			);

	HEL_CHECK(send_resp.error());
	logBragiReply(resp);
	co_return {};
}

async::result<std::expected<void, DispatchError>>
HandleRequest::operator()(managarm::posix::UnmountRequest &&req,
		helix::BorrowedDescriptor conversation, bragi::preamble preamble,
		std::shared_ptr<Process> self, std::shared_ptr<Generation>) {
	id = preamble.id();

	auto tailRes = co_await dispatchTail(req, conversation, preamble);
	if(!tailRes)
		co_return std::unexpected(tailRes.error());
	logBragiRequest(req);

	logRequest(logRequests, self, "UMOUNT", "target={}", req.target_path());

	if(self->threadGroup()->uid() != 0) {
		co_await sendErrorResponse<managarm::posix::UnmountResponse>(conversation, managarm::posix::Errors::INSUFFICIENT_PERMISSION);
		co_return {};
	}

	auto resolveResult = co_await resolve(self->fsContext()->getRoot(),
			self->fsContext()->getWorkingDirectory(), req.target_path(), self.get());
	if(!resolveResult) {
		if(resolveResult.error() == protocols::fs::Error::fileNotFound)
			co_await sendErrorResponse<managarm::posix::UnmountResponse>(conversation, managarm::posix::Errors::FILE_NOT_FOUND);
		else
			co_await sendErrorResponse<managarm::posix::UnmountResponse>(conversation, managarm::posix::Errors::ILLEGAL_ARGUMENTS);
		co_return {};
	}
	auto target = resolveResult.value();

	// resolve() crosses into a mount at the final component, so a mount point comes
	// back as the child MountView whose .second is its own origin root. Anything else
	// (a non-mounted directory, or the VFS root which has no parent) is not something
	// we can unmount -- Linux returns EINVAL there.
	auto mountView = target.first;
	if(!mountView->getParent() || target.second != mountView->getOrigin()) {
		co_await sendErrorResponse<managarm::posix::UnmountResponse>(conversation, managarm::posix::Errors::ILLEGAL_ARGUMENTS);
		co_return {};
	}

	auto unmountResult = co_await mountView->getParent()->unmount(mountView->getAnchor());
	if(!unmountResult) {
		co_await sendErrorResponse<managarm::posix::UnmountResponse>(conversation, unmountResult.error() | toPosixProtoError);
		co_return {};
	}

	logRequest(logRequests, self, "UMOUNT", "succeeded");

	managarm::posix::UnmountResponse resp;
	resp.set_error(managarm::posix::Errors::SUCCESS);

	auto [send_resp] = co_await helix_ng::exchangeMsgs(
				conversation,
				helix_ng::sendBragiHeadOnly(resp, frg::stl_allocator{})
			);

	HEL_CHECK(send_resp.error());
	logBragiReply(resp);
	co_return {};
}

async::result<std::expected<void, DispatchError>>
HandleRequest::operator()(managarm::posix::SysconfRequest &&req,
		helix::BorrowedDescriptor conversation, bragi::preamble preamble,
		std::shared_ptr<Process> self, std::shared_ptr<Generation>) {
	id = preamble.id();
	logBragiRequest(req);

	logRequest(logRequests, self, "SYSCONF");

	managarm::posix::SysconfResponse resp;

	// Configured == available == online
	if(req.num() == _SC_NPROCESSORS_CONF || req.num() == _SC_NPROCESSORS_ONLN) {
		managarm::kerncfg::GetNumCpuRequest kerncfgRequest;
		auto [offer, kerncfgSendResp, kerncfgResp] = co_await helix_ng::exchangeMsgs(
		getKerncfgLane(),
		helix_ng::offer(
				helix_ng::sendBragiHeadOnly(kerncfgRequest, frg::stl_allocator{}),
				helix_ng::recvInline()
			)
		);
		HEL_CHECK(offer.error());
		HEL_CHECK(kerncfgSendResp.error());
		HEL_CHECK(kerncfgResp.error());

		auto kernResp = bragi::parse_head_only<managarm::kerncfg::GetNumCpuResponse>(kerncfgResp);
		kerncfgResp.reset();

		resp.set_error(managarm::posix::Errors::SUCCESS);
		resp.set_value(kernResp->num_cpu());
	} else {
		// Not handled, bubble up EINVAL.
		resp.set_error(managarm::posix::Errors::ILLEGAL_ARGUMENTS);
	}
	auto [send_resp] = co_await helix_ng::exchangeMsgs(
		conversation,
		helix_ng::sendBragiHeadOnly(resp, frg::stl_allocator{})
	);

	HEL_CHECK(send_resp.error());
	logBragiReply(resp);
	co_return {};
}

async::result<std::expected<void, DispatchError>>
HandleRequest::operator()(managarm::posix::GetMemoryInformationRequest &&req,
		helix::BorrowedDescriptor conversation, bragi::preamble preamble,
		std::shared_ptr<Process> self, std::shared_ptr<Generation>) {
	id = preamble.id();
	logBragiRequest(req);

	logRequest(logRequests, self, "GET_MEMORY_INFORMATION");

	managarm::kerncfg::GetMemoryInformationRequest kerncfgRequest;
	auto [offer, kerncfgSendResp, kerncfgResp] = co_await helix_ng::exchangeMsgs(
		getKerncfgLane(),
		helix_ng::offer(
			helix_ng::sendBragiHeadOnly(kerncfgRequest, frg::stl_allocator{}),
			helix_ng::recvInline()
		)
	);
	HEL_CHECK(offer.error());
	HEL_CHECK(kerncfgSendResp.error());
	HEL_CHECK(kerncfgResp.error());

	auto kernResp = bragi::parse_head_only<managarm::kerncfg::GetMemoryInformationResponse>(kerncfgResp);
	kerncfgResp.reset();

	managarm::posix::GetMemoryInformationResponse resp;
	resp.set_total_usable_memory(kernResp->total_usable_memory());
	resp.set_available_memory(kernResp->available_memory());
	resp.set_memory_unit(kernResp->memory_unit());

	auto [sendResp] = co_await helix_ng::exchangeMsgs(
		conversation,
		helix_ng::sendBragiHeadOnly(resp, frg::stl_allocator{})
	);
	HEL_CHECK(sendResp.error());
	logBragiReply(resp);
	co_return {};
}

} // namespace requests
