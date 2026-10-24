#pragma once
#include <AYEventSystem/EventPriority.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <typeindex>
#include <vector>

namespace ayt::event {
/// Orthogonal to payload type and priority. Empty channel is the legacy/default
/// channel. Names are exact, case-sensitive strings; dots delimit channel trees.
struct EventMetadata {
    std::string channel, scope, source, correlation, eventId;
    std::int64_t occurredAtMs = 0; // caller's clock; zero means unspecified
    std::vector<std::string> tags;
};
/// AND between fields, OR within each list, ALL required tags. Empty fields
/// impose no restriction. This is routing, never an authorization boundary.
struct EventFilter {
    std::vector<std::string> channels, scopes, sources, requiredTags;
    std::string channelTree; // root itself or root + '.' + descendant
    bool matches(const EventMetadata& m) const {
        auto accepts = [](const auto& values, const auto& v) {
            return values.empty() || std::find(values.begin(), values.end(), v) != values.end();
        };
        return accepts(channels,m.channel) && accepts(scopes,m.scope) && accepts(sources,m.source)
            && (channelTree.empty() || m.channel == channelTree ||
                (m.channel.size() > channelTree.size() && m.channel.starts_with(channelTree)
                    && m.channel[channelTree.size()] == '.'))
            && std::all_of(requiredTags.begin(),requiredTags.end(),[&](const auto& t) {
                return std::find(m.tags.begin(),m.tags.end(),t) != m.tags.end();
            });
    }
};
/// Borrowed only during the callback. Copy metadata or an explicit serialized
/// snapshot to retain an event. Numeric type IDs are not persistence identifiers.
struct EventView {
    EventTypeId typeId;
    const EventMetadata& metadata;
    std::type_index payloadType;
    const void* payload;
    template<class T> const T* get() const noexcept {
        return payloadType == std::type_index(typeid(T)) ? static_cast<const T*>(payload) : nullptr;
    }
};
}
