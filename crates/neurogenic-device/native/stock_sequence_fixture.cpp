#define NEUROGENIC_NATIVE_FIXTURE 1
#include "stock_sequence.cpp"

#include <cassert>
#include <cstdio>

namespace {
alignas(16) unsigned char screen_storage[0x200]{};
alignas(16) unsigned char controller_storage[0x300]{};
alignas(16) unsigned char things_storage[0x300]{};
alignas(16) unsigned char sequence_storage[0x20]{};
alignas(16) unsigned char program_storage[0x20]{};
alignas(16) unsigned char track_storage[0x20]{};
alignas(16) unsigned char other_track_storage[0x20]{};
alignas(16) unsigned char list_storage[0x100]{};
alignas(16) unsigned char service_storage[0x20]{};
alignas(16) unsigned char event_node[0x20 + sizeof(Event)]{};
constexpr std::uintptr_t BASE = 0x10000000;
int inserts, frees, strings, drops, releases, adds, removes;
bool fail_tree_insert;
Event last_event{};

int visible(void *) { return 1; }
int hidden(void *) { return 0; }
int pad_to_note(void *, int pad) { return 36 + pad; }
std::int64_t sequence_length(void *) { return 3840; }
Event construct(unsigned note, std::int64_t tick, std::int64_t duration,
                unsigned value, unsigned flags, float velocity) {
    assert(note < 128 && duration == 240 && value == 100 && flags == 0);
    Event event{}; event.time = tick; event.type = 3;
    std::memcpy(event.data, &velocity, sizeof(velocity)); event.data[0x20] = static_cast<unsigned char>(note);
    return event;
}
void tree_insert(void *tree_pointer, const void *event_pointer) {
    auto &tree = *static_cast<NativeTree *>(tree_pointer);
    assert(tree.left == &tree.colour && tree.right == &tree.colour);
    tree.parent = reinterpret_cast<void *>(0x1234); tree.size = 1;
    if (fail_tree_insert) throw 7;
    std::memcpy(&last_event, event_pointer, sizeof(last_event)); ++inserts;
}
void tree_free(void *node) { assert(node == reinterpret_cast<void *>(0x1234)); ++frees; }
void string_construct(std::uint64_t *out, const char *) {
    *out = 1; ++strings;
}
void string_drop(std::uint64_t *value) { assert(*value == 1); *value = 0; ++drops; }
void release_shared(void *control) { assert(control == reinterpret_cast<void *>(0x5678)); ++releases; }
void *tree_next(void *) { return list_storage + 0x68; }

struct SharedResult {
    void *object;
    void *control;
    ~SharedResult() {}
};
SharedResult add_command(void *service, void *list, void *, void *added, void *,
                         std::uint64_t *, std::uint64_t *, int, void *) {
    assert(service == service_storage && list == list_storage);
    assert(static_cast<NativeTree *>(added)->size == 1); ++adds;
    return {reinterpret_cast<void *>(0x1111), reinterpret_cast<void *>(0x5678)};
}
SharedResult remove_command(void *service, void *list, const void *event, const void *) {
    assert(service == service_storage && list == list_storage);
    assert(std::memcmp(event, &last_event, sizeof(last_event)) == 0); ++removes;
    return {reinterpret_cast<void *>(0x2222), reinterpret_cast<void *>(0x5678)};
}

void reset_context() {
    std::memset(screen_storage, 0, sizeof(screen_storage));
    std::memset(controller_storage, 0, sizeof(controller_storage));
    std::memset(things_storage, 0, sizeof(things_storage));
    field<std::uintptr_t>(screen_storage, 0) = BASE + STEP_SCREEN_VTABLE;
    field<void *>(screen_storage, 0x108) = controller_storage;
    field<std::uintptr_t>(controller_storage, 0) = BASE + STEP_CONTROLLER_VTABLE;
    field<void *>(controller_storage, 0xc0) = list_storage;
    field<void *>(controller_storage, 0x240) = service_storage;
    field<void *>(controller_storage, 0x248) = things_storage;
    field<void *>(things_storage, 0x1c8) = sequence_storage;
    field<void *>(things_storage, 0xb8) = program_storage;
    field<void *>(things_storage, 0x140) = track_storage;
    field<void *>(list_storage, 0x78) = event_node;
}
}

extern "C" void *neurogenic_fixture_function(std::uintptr_t offset) {
    switch (offset) {
    case SCREEN_VISIBLE: return reinterpret_cast<void *>(visible);
    case SCREEN_HIDDEN: return reinterpret_cast<void *>(hidden);
    case PAD_TO_NOTE: return reinterpret_cast<void *>(pad_to_note);
    case SEQUENCE_LENGTH: return reinterpret_cast<void *>(sequence_length);
    case NOTE_CONSTRUCTOR: return reinterpret_cast<void *>(construct);
    case TREE_INSERT: return reinterpret_cast<void *>(tree_insert);
    case TREE_FREE: return reinterpret_cast<void *>(tree_free);
    case STRING_CONSTRUCT: return reinterpret_cast<void *>(string_construct);
    case STRING_DROP: return reinterpret_cast<void *>(string_drop);
    case ADD_COMMAND: return reinterpret_cast<void *>(add_command);
    case REMOVE_COMMAND: return reinterpret_cast<void *>(remove_command);
    case RELEASE_SHARED: return reinterpret_cast<void *>(release_shared);
    case TREE_NEXT: return reinterpret_cast<void *>(tree_next);
    default: return nullptr;
    }
}

extern "C" void neurogenic_m04a_step_paint(void *, void *) {}

int main() {
    reset_context();
    std::uint64_t initial_identity[2]{}, changed_identity[2]{};
    assert(neurogenic_stock_sequence_context(BASE, screen_storage, initial_identity) == 0);
    Request request{3, 100, 0, 480, 240}; Receipt receipt{};
    assert(neurogenic_stock_sequence_insert(BASE, screen_storage, &request, &receipt) == 0);
    assert(receipt.note == 39 && receipt.event.time == 480 && (receipt.event.type & 255) == 3);
    assert(inserts == 1 && adds == 1 && frees == 1 && strings == 2 && drops == 2 && releases == 1);
    std::memcpy(event_node + 0x20, &receipt.event, sizeof(receipt.event));
    assert(neurogenic_stock_sequence_contains(BASE, screen_storage, &receipt) == 1);
    assert(neurogenic_stock_sequence_insert(BASE, screen_storage, &request, &receipt) == 2);
    field<void *>(things_storage, 0x140) = other_track_storage;
    assert(neurogenic_stock_sequence_context(BASE, screen_storage, changed_identity) == 0);
    assert(std::memcmp(initial_identity, changed_identity, sizeof(initial_identity)) != 0);
    assert(neurogenic_stock_sequence_contains(BASE, screen_storage, &receipt) == -1);
    assert(neurogenic_stock_sequence_remove(BASE, screen_storage, &receipt) == 1);
    field<void *>(things_storage, 0x140) = track_storage;
    event_node[0x20] ^= 1;
    assert(neurogenic_stock_sequence_contains(BASE, screen_storage, &receipt) == 0);
    event_node[0x20] ^= 1;
    assert(neurogenic_stock_sequence_remove(BASE, screen_storage, &receipt) == 0);
    assert(removes == 1 && releases == 2);

    const int frees_before_failure = frees;
    request.tick = 720; fail_tree_insert = true;
    assert(neurogenic_stock_sequence_insert(BASE, screen_storage, &request, &receipt) == 3);
    assert(frees == frees_before_failure + 1);
    fail_tree_insert = false;

    field<std::uintptr_t>(screen_storage, 0) = 0;
    assert(neurogenic_stock_sequence_insert(BASE, screen_storage, &request, &receipt) == 1);
    reset_context(); request.tick = 3840;
    assert(neurogenic_stock_sequence_insert(BASE, screen_storage, &request, &receipt) == 2);
    request.tick = 0; request.physical_pad = 16;
    assert(neurogenic_stock_sequence_insert(BASE, screen_storage, &request, &receipt) == 2);

    std::puts("PASS native tag-3 construction, ownership/cleanup, duplicate rejection, context identity, exact remove, and ABI dispatch");
}
