#pragma once

// ----------------------------------------------------------------
// Range allocator.
// ----------------------------------------------------------------

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <optional>
#include <set>

struct range_allocator {
private:
	struct node {
		uint64_t off;
		unsigned int ord;

		friend bool operator< (const node &u, const node &v) {
			if(u.ord != v.ord)
				return u.ord < v.ord;
			return u.off < v.off;
		}
	};

	static unsigned int clz(unsigned long x) {
		return __builtin_clzl(x);
	}

public:
	static unsigned int round_order(size_t size) {
		assert(size >= 1);
		if(size == 1)
			return 0;
		return CHAR_BIT * sizeof(size_t) - clz(size - 1);
	}

	range_allocator(unsigned int order, unsigned int granularity)
	: _granularity{granularity} {
		_nodes.insert(node{0, order});
	}

	// Exhaustion is a normal condition, not a programming error: the pool is finite
	// and the request sizes are driven by clients. Prefer the try_ forms anywhere a
	// client can influence the size -- an assert in a driver raises a signal that
	// kills the whole server (this is DEF-30: gfx_bochs died on exactly that path).
	std::optional<uint64_t> try_allocate(size_t size) {
		return try_allocate_order(std::max(_granularity, round_order(size)));
	}

	std::optional<uint64_t> try_allocate_order(unsigned int order) {
		if(order < _granularity)
			return std::nullopt;

		auto it = _nodes.lower_bound(node{0, order});
		if(it == _nodes.end())
			return std::nullopt;

		auto offset = it->off;

		while(it->ord != order) {
			assert(it->ord > order);
			auto high = _nodes.insert(it,
					node{it->off + (uint64_t(1) << (it->ord - 1)), it->ord - 1});
			auto low = _nodes.insert(high, node{it->off, it->ord - 1});
			_nodes.erase(it);
			it = low;
		}
		_nodes.erase(it);

		return offset;
	}

	// Asserting wrappers, kept for callers that genuinely cannot fail. These share
	// the implementation above so the two cannot drift apart.
	uint64_t allocate(size_t size) {
		return allocate_order(std::max(_granularity, round_order(size)));
	}

	uint64_t allocate_order(unsigned int order) {
		assert(order >= _granularity);
		auto offset = try_allocate_order(order);
		assert(offset);
		return *offset;
	}

	// The size passed here MUST be the same one passed to allocate(), not the
	// caller's logical object size -- round_order() has to land on the same order or
	// the buddy tree is corrupted.
	void free(uint64_t offset, size_t size) {
		return free_order(offset, std::max(_granularity, round_order(size)));
	}

	// NOTE: buddies are not coalesced. A freed block returns to the pool at its own
	// order and is never merged back into a larger one, so a workload that allocates
	// and frees a mix of sizes fragments monotonically and can fail to satisfy a
	// large request while holding ample total free space. Same-size churn -- the
	// common case here, since buffers track the display mode -- reuses cleanly and is
	// unaffected. Left as-is deliberately: coalescing needs a buddy lookup this
	// structure does not support, and no current workload needs it.
	void free_order(uint64_t offset, unsigned int order) {
		assert(order >= _granularity);

		_nodes.insert(node{offset, order});
	}

private:
	std::set<node> _nodes;
	unsigned int _granularity;
};

