#ifndef TTL_H
#define TTL_H

#include "kvstore.h"
#include <stdint.h>

// active expiry sweep; call from event loop; stops after budget_ms elapses
void ttl_sweep(kvstore *kv, int64_t budget_ms);

#endif /* TTL_H */
