#ifndef JEMALLOC_INTERNAL_TICKER_H
#define JEMALLOC_INTERNAL_TICKER_H

#include "nvalloc/internal/util.h"

/**
 * A ticker makes it easy to count-down events until some limit.  You
 * ticker_init the ticker to trigger every nticks events.  You then notify it
 * that an event has occurred with calls to ticker_tick (or that nticks events
 * have occurred with a call to ticker_ticks), which will return true (and reset
 * the counter) if the countdown hit zero.
 */

typedef struct {
	int32_t tick;
	int32_t nticks;
} ticker_t;

static inline void
ticker_init(ticker_t *ticker, int32_t nticks) {
	ticker->tick = nticks;
	ticker->nticks = nticks;
}

static inline void
ticker_copy(ticker_t *ticker, const ticker_t *other) {
	*ticker = *other;
}

static inline int32_t
ticker_read(const ticker_t *ticker) {
	return ticker->tick;
}

__attribute__((noinline))
static bool
ticker_fixup(ticker_t *ticker) {
	ticker->tick = ticker->nticks;
	return true;
}

static inline bool
ticker_ticks(ticker_t *ticker, int32_t nticks) {
	ticker->tick -= nticks;
	if (unlikely(ticker->tick < 0)) {
		return ticker_fixup(ticker);
	}
	return false;
}

static inline bool
ticker_tick(ticker_t *ticker) {
	return ticker_ticks(ticker, 1);
}

/* 
 * Try to tick.  If ticker would fire, return true, but rely on
 * slowpath to reset ticker.
 */
static inline bool
ticker_trytick(ticker_t *ticker) {
	--ticker->tick;
	if (unlikely(ticker->tick < 0)) {
		return true;
	}
	return false;
}

#endif /* JEMALLOC_INTERNAL_TICKER_H */
