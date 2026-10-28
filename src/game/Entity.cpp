#include "game/Entity.hpp"

#include "game/Offsets.hpp"
#include "memory/Access.hpp"

namespace lyrics::game::entity {
namespace {

namespace layout = offsets::entity;
constexpr int kMaxChain = 64;

bool find_storage(std::uintptr_t registry, std::uint32_t hash, std::uintptr_t& storage) {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    std::uintptr_t nodes = 0;
    std::uintptr_t nodes_end = 0;
    if (!memory::read(registry + layout::BucketsBegin, begin) || !memory::read(registry + layout::BucketsEnd, end) ||
        end <= begin || !memory::read(registry + layout::NodesBegin, nodes) ||
        !memory::read(registry + layout::NodesEnd, nodes_end)) {
        return false;
    }
    const std::uint64_t count = (end - begin) / sizeof(std::uint64_t);
    std::uint64_t index = 0;
    if (!memory::read(begin + (hash & (count - 1)) * sizeof(std::uint64_t), index)) {
        return false;
    }
    for (int step = 0; step < kMaxChain && index != ~std::uint64_t{0}; ++step) {
        const std::uintptr_t node = nodes + index * layout::NodeSize;
        std::uint32_t key = 0;
        if (node >= nodes_end || !memory::read(node + layout::NodeKey, key)) {
            return false;
        }
        if (key == hash) {
            return memory::read(node + layout::NodeStorage, storage) && storage != 0;
        }
        if (!memory::read(node, index)) {
            return false;
        }
    }
    return false;
}

bool sparse_index(std::uintptr_t storage, std::uint32_t identifier, std::uint32_t& index) {
    std::uintptr_t sparse = 0;
    std::uintptr_t sparse_end = 0;
    if (!memory::read(storage + layout::SparseBegin, sparse) || !memory::read(storage + layout::SparseEnd, sparse_end) ||
        sparse_end < sparse) {
        return false;
    }
    const std::uint32_t entity = identifier & layout::EntityMask;
    const std::uintptr_t page_index = entity >> 11;
    std::uintptr_t page = 0;
    std::uint32_t entry = 0;
    if (page_index >= (sparse_end - sparse) / sizeof(std::uintptr_t) ||
        !memory::read(sparse + page_index * sizeof(std::uintptr_t), page) || page == 0 ||
        !memory::read(page + (entity & 0x7FF) * sizeof(std::uint32_t), entry)) {
        return false;
    }
    // a different version in the slot means the entity it named is gone
    if ((entry ^ (identifier & layout::VersionMask)) >= layout::EntityMask) {
        return false;
    }
    index = entry & layout::EntityMask;
    return true;
}

}

std::uintptr_t find_component(std::uintptr_t actor, std::uint32_t hash, std::size_t size) {
    std::uintptr_t registry = 0;
    std::uint32_t identifier = 0;
    std::uintptr_t storage = 0;
    std::uint32_t index = 0;
    std::uintptr_t pages = 0;
    std::uintptr_t page = 0;
    if (actor == 0 || !memory::read(actor + layout::ContextRegistry, registry) ||
        !memory::read(actor + layout::ContextIdentifier, identifier) || !find_storage(registry, hash, storage) ||
        !sparse_index(storage, identifier, index) || !memory::read(storage + layout::PackedPages, pages) ||
        !memory::read(pages + (index >> 7) * sizeof(std::uintptr_t), page) || page == 0) {
        return 0;
    }
    return page + (index & 0x7F) * size;
}

}
