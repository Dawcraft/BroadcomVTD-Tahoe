#pragma once
#include "TraceFormat.hpp"
#include "MapperSelection.hpp"
#include <stddef.h>

namespace bvp {
extern uint32_t gateStatus;
extern uint32_t runtimeModeInfo;
inline bool correctiveMode() {
    return __atomic_load_n(&runtimeModeInfo,__ATOMIC_ACQUIRE)==uint32_t(RuntimeMode::AppleVTDCorrectiveExperimental);
}
extern uint64_t providerID, controllerID;
void initializeTrace();
void setRxSnapshot(void (*callback)(unsigned,Event &));
void emit(Event &event);
Event event(uint32_t type, const void *object = nullptr, const void *packet = nullptr,
            const void *auxiliary = nullptr, uint32_t flags = 0);
void freezeSoon();
enum TagKind : uint32_t { Osl = 1, PoolTag, PacketTag, WlcTag };
bool tagSet(const void *key, TagKind kind, uint64_t value);
uint64_t tagGet(const void *key, TagKind kind);
void tagErase(const void *key, TagKind kind);
void countTableDrop();
} // namespace bvp
