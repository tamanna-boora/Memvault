#ifndef EVICTION_H
#define EVICTION_H

struct kv_server; // forward decl to avoid circular include

// evicts with allkeys-lru until under limit; no-op when maxmemory is 0
void evict_if_needed(struct kv_server *s);

#endif /* EVICTION_H */
