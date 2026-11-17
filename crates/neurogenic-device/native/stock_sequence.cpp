/*
 * Minimal stock sequence command boundary for MPC Sample 1.3.0.12.
 * The native container/string/command pattern is adapted from AM Mod's
 * MIT-licensed step_native.c and step_exceptions.cc at commit
 * 6cdc67276f43cd39f64871cb7e36782481244c65.
 * Copyright (c) 2026 AM Mod contributors.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
constexpr std::uintptr_t STEP_SCREEN_VTABLE = 0x06e24f38;
constexpr std::uintptr_t STEP_CONTROLLER_VTABLE = 0x06d8ec20;
constexpr std::uintptr_t SCREEN_VISIBLE = 0x0112fd50;
constexpr std::uintptr_t SCREEN_HIDDEN = 0x0112f6b0;
constexpr std::uintptr_t PAD_TO_NOTE = 0x0157f800;
constexpr std::uintptr_t SEQUENCE_LENGTH = 0x029571f0;
constexpr std::uintptr_t NOTE_CONSTRUCTOR = 0x027e54d0;
constexpr std::uintptr_t TREE_INSERT = 0x01574460;
constexpr std::uintptr_t TREE_FREE = 0x0157df10;
constexpr std::uintptr_t STRING_CONSTRUCT = 0x00f535c0;
constexpr std::uintptr_t STRING_DROP = 0x00f0a290;
constexpr std::uintptr_t ADD_COMMAND = 0x019500e0;
constexpr std::uintptr_t REMOVE_COMMAND = 0x0194fa30;
constexpr std::uintptr_t RELEASE_SHARED = 0x0123bca0;
constexpr std::uintptr_t TREE_NEXT = 0x00b51738;

template <typename T> T &field(void *pointer, std::size_t offset) {
    return *reinterpret_cast<T *>(static_cast<unsigned char *>(pointer) + offset);
}
#ifdef NEUROGENIC_NATIVE_FIXTURE
extern "C" void *neurogenic_fixture_function(std::uintptr_t offset);
template <typename T> T function(std::uintptr_t, std::uintptr_t offset) {
    return reinterpret_cast<T>(neurogenic_fixture_function(offset));
}
#else
template <typename T> T function(std::uintptr_t base, std::uintptr_t offset) {
    return reinterpret_cast<T>(base + offset);
}
#endif

struct Event {
    std::int64_t time;
    std::uint32_t type;
    std::uint32_t reserved;
    unsigned char data[40];
};
static_assert(sizeof(Event) == 56);
static_assert(offsetof(Event, data) == 16);

struct NativeTree {
    std::uint64_t padding;
    std::uint64_t colour;
    void *parent;
    void *left;
    void *right;
    std::uint64_t size;
};
static_assert(sizeof(NativeTree) == 48);

struct NativeResult {
    std::uintptr_t object;
    std::uintptr_t control;
};

struct Request {
    std::uint8_t physical_pad;
    std::uint8_t velocity;
    std::uint16_t reserved;
    std::int64_t tick;
    std::int64_t duration;
};
static_assert(sizeof(Request) == 24);

struct Receipt {
    Event event;
    std::int64_t sequence_length;
    std::uint32_t note;
    std::uint32_t reserved;
    std::uint64_t context_identity[2];
};
static_assert(sizeof(Receipt) == 88);

struct Context {
    void *controller;
    void *sequence;
    void *program;
    void *track;
    void *event_list;
    void *service;
    std::int64_t sequence_length;
};

extern "C" void neurogenic_step_add_result(void *, void *, void *, void *, void *,
                                             std::uint64_t *, std::uint64_t *, int,
                                             void *, std::uintptr_t);
extern "C" void neurogenic_step_remove_result(void *, void *, const void *, const void *,
                                                void *, std::uintptr_t);

void tree_init(NativeTree &tree) {
    std::memset(&tree, 0, sizeof(tree));
    tree.left = tree.right = &tree.colour;
}

void release_result(std::uintptr_t base, NativeResult &result) {
    if (result.control != 0) {
        function<void (*)(void *)>(base, RELEASE_SHARED)(reinterpret_cast<void *>(result.control));
        result = {};
    }
}

bool context(std::uintptr_t base, void *screen, Context &out) {
    if (base == 0 || screen == nullptr || field<std::uintptr_t>(screen, 0) != base + STEP_SCREEN_VTABLE) {
        return false;
    }
    if (function<int (*)(void *)>(base, SCREEN_VISIBLE)(screen) == 0 ||
        function<int (*)(void *)>(base, SCREEN_HIDDEN)(screen) != 0) {
        return false;
    }
    void *controller = field<void *>(screen, 0x108);
    if (controller == nullptr || field<std::uintptr_t>(controller, 0) != base + STEP_CONTROLLER_VTABLE) {
        return false;
    }
    void *things = field<void *>(controller, 0x248);
    void *sequence = things == nullptr ? nullptr : field<void *>(things, 0x1c8);
    void *program = things == nullptr ? nullptr : field<void *>(things, 0xb8);
    void *track = things == nullptr ? nullptr : field<void *>(things, 0x140);
    void *event_list = field<void *>(controller, 0xc0);
    void *service = field<void *>(controller, 0x240);
    if (sequence == nullptr || program == nullptr || track == nullptr ||
        event_list == nullptr || service == nullptr) {
        return false;
    }
    const std::int64_t length = function<std::int64_t (*)(void *)>(base, SEQUENCE_LENGTH)(sequence);
    if (length <= 0 || length > 960LL * 128 * 64) {
        return false;
    }
    out = {controller, sequence, program, track, event_list, service, length};
    return true;
}

std::uint64_t mix(std::uint64_t value) {
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

void context_identity(const Context &live, std::uint64_t out[2]) {
    const std::uint64_t values[] = {
        reinterpret_cast<std::uintptr_t>(live.sequence),
        reinterpret_cast<std::uintptr_t>(live.track),
        reinterpret_cast<std::uintptr_t>(live.event_list),
        reinterpret_cast<std::uintptr_t>(live.program),
    };
    std::uint64_t a = UINT64_C(0x243f6a8885a308d3);
    std::uint64_t b = UINT64_C(0x13198a2e03707344);
    for (unsigned i = 0; i < 4; ++i) {
        a = mix(a ^ mix(values[i] + UINT64_C(0x9e3779b97f4a7c15) * (i + 1)));
        b = mix(b + mix(values[3 - i] ^ UINT64_C(0xd1b54a32d192ed03) * (i + 1)));
    }
    out[0] = a;
    out[1] = b;
}

bool same_context(const Context &live, const Receipt &receipt) {
    std::uint64_t current[2]{};
    context_identity(live, current);
    return live.sequence_length == receipt.sequence_length &&
           std::memcmp(current, receipt.context_identity, sizeof(current)) == 0;
}

int contains_event(std::uintptr_t base, const Context &live, const Event &wanted) {
    void *end = static_cast<unsigned char *>(live.event_list) + 0x68;
    void *node = field<void *>(live.event_list, 0x78);
    unsigned budget = 4096;
    while (node != end && budget-- != 0) {
        if (node == nullptr) return -3;
        const void *event = static_cast<unsigned char *>(node) + 0x20;
        if (std::memcmp(event, &wanted, sizeof(Event)) == 0) return 1;
        node = function<void *(*)(void *)>(base, TREE_NEXT)(node);
    }
    return node == end ? 0 : -3;
}

void cleanup(std::uintptr_t base, NativeTree &removed, NativeTree &added,
             NativeTree &changed, std::uint64_t &title, std::uint64_t &merge,
             NativeResult &result) {
    release_result(base, result);
    if (title != 0) function<void (*)(std::uint64_t *)>(base, STRING_DROP)(&title);
    if (merge != 0) function<void (*)(std::uint64_t *)>(base, STRING_DROP)(&merge);
    if (added.parent != nullptr) function<void (*)(void *)>(base, TREE_FREE)(added.parent);
    if (removed.parent != nullptr) function<void (*)(void *)>(base, TREE_FREE)(removed.parent);
    if (changed.parent != nullptr) function<void (*)(void *)>(base, TREE_FREE)(changed.parent);
}
} // namespace

extern "C" int neurogenic_stock_sequence_context(std::uintptr_t base, void *screen,
                                                     std::uint64_t out[2]) noexcept {
    if (out == nullptr) return 2;
    try {
        Context live{};
        if (!context(base, screen, live)) return 1;
        context_identity(live, out);
        return 0;
    } catch (...) {
        return 3;
    }
}

extern "C" int neurogenic_stock_sequence_insert(std::uintptr_t base, void *screen,
                                                   const Request *request, Receipt *receipt) noexcept {
    if (request == nullptr || receipt == nullptr || request->physical_pad >= 16 ||
        request->velocity == 0 || request->velocity > 127 || request->duration <= 0) {
        return 2;
    }
    Context live{};
    try {
        if (!context(base, screen, live)) return 1;
        if (request->tick < 0 || request->tick >= live.sequence_length) return 2;
        const int note = function<int (*)(void *, int)>(base, PAD_TO_NOTE)(live.controller, request->physical_pad);
        if (note < 0 || note > 127) return 2;

        Event event = function<Event (*)(unsigned, std::int64_t, std::int64_t, unsigned, unsigned, float)>(
            base, NOTE_CONSTRUCTOR)(static_cast<unsigned>(note), request->tick, request->duration,
                                    100, 0, static_cast<float>(request->velocity) / 127.0F);
        if ((event.type & 0xffU) != 3 || event.time != request->tick) return 3;
        const int already_present = contains_event(base, live, event);
        if (already_present != 0) return already_present > 0 ? 2 : 3;

        NativeTree removed{}, added{}, changed{};
        tree_init(removed); tree_init(added); tree_init(changed);
        std::uint64_t title = 0, merge = 0;
        NativeResult result{};
        try {
            function<void (*)(void *, const void *)>(base, TREE_INSERT)(&added, &event);
            function<void (*)(std::uint64_t *, const char *)>(base, STRING_CONSTRUCT)(&title, "NEUROGENIC Add Step");
            function<void (*)(std::uint64_t *, const char *)>(base, STRING_CONSTRUCT)(&merge, "");
            neurogenic_step_add_result(live.service, live.event_list, &removed, &added, &changed,
                                       &title, &merge, 0, &result,
                                       reinterpret_cast<std::uintptr_t>(function<void *>(base, ADD_COMMAND)));
        } catch (...) {
            cleanup(base, removed, added, changed, title, merge, result);
            return 3;
        }
        cleanup(base, removed, added, changed, title, merge, result);
        std::memcpy(&receipt->event, &event, sizeof(event));
        receipt->sequence_length = live.sequence_length;
        receipt->note = static_cast<std::uint32_t>(note);
        receipt->reserved = 0;
        context_identity(live, receipt->context_identity);
        return 0;
    } catch (...) {
        return 3;
    }
}

extern "C" int neurogenic_stock_sequence_remove(std::uintptr_t base, void *screen,
                                                   const Receipt *receipt) noexcept {
    if (receipt == nullptr || (receipt->event.type & 0xffU) != 3) return 2;
    try {
        Context live{};
        if (!context(base, screen, live) || !same_context(live, *receipt)) return 1;
        NativeResult result{};
        try {
            neurogenic_step_remove_result(live.service, live.event_list, &receipt->event, nullptr,
                                          &result,
                                          reinterpret_cast<std::uintptr_t>(function<void *>(base, REMOVE_COMMAND)));
        } catch (...) {
            release_result(base, result);
            return 3;
        }
        release_result(base, result);
        return 0;
    } catch (...) {
        return 3;
    }
}

extern "C" int neurogenic_stock_sequence_contains(std::uintptr_t base, void *screen,
                                                     const Receipt *receipt) noexcept {
    if (receipt == nullptr || (receipt->event.type & 0xffU) != 3) return -2;
    try {
        Context live{};
        if (!context(base, screen, live) || !same_context(live, *receipt)) return -1;
        return contains_event(base, live, receipt->event);
    } catch (...) {
        return -3;
    }
}
