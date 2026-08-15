#pragma once
// AYEventSystem/Connection.h
//
// Opaque subscription token + RAII handle.
//
// Unsubscribe is type-erased: ScopedConnection::reset() calls
// EventBus::unsubscribe(id), which looks up the owning type bucket
// by id alone. No std::function::target comparison (Anti-Goal §1.3).

#include <cstdint>

namespace ayt::event
{

class EventBus;

// 0 = invalid; ids are unique across the bus instance (not just per-type).
using ConnectionId = uint64_t;
constexpr ConnectionId kInvalidConnectionId = 0;

// Move-only RAII handle. Calls unsubscribe on destruction / reset.
class ScopedConnection {
public:
    ScopedConnection() = default;

    ScopedConnection(EventBus& bus, ConnectionId id) noexcept
        : _bus(&bus)
        , _id(id)
    {}

    ScopedConnection(ScopedConnection&& other) noexcept
        : _bus(other._bus)
        , _id(other._id)
    {
        other._bus = nullptr;
        other._id = kInvalidConnectionId;
    }

    ScopedConnection& operator=(ScopedConnection&& other) noexcept {
        if (this != &other) {
            reset();
            _bus = other._bus;
            _id = other._id;
            other._bus = nullptr;
            other._id = kInvalidConnectionId;
        }
        return *this;
    }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    ~ScopedConnection() { reset(); }

    // Idempotent: calling twice is a no-op the second time.
    void reset();

    ConnectionId id() const { return _id; }
    bool isValid() const { return _id != kInvalidConnectionId && _bus != nullptr; }
    explicit operator bool() const { return isValid(); }

    // Release ownership without unsubscribing; returns the id.
    // Caller is responsible for unsubscribe if desired.
    ConnectionId release() noexcept {
        ConnectionId r = _id;
        _id = kInvalidConnectionId;
        _bus = nullptr;
        return r;
    }

private:
    EventBus* _bus = nullptr;
    ConnectionId _id = kInvalidConnectionId;
};

} // namespace ayt::event