#include "event_api.h"

__attribute__((visibility("default")))
int fixture_predicate(void);

__attribute__((constructor))
static void fixture_initializer(void) {
	fixture_record("initializer", "library constructor entered");
	fixture_record("initializer-before-predicate", "calling predicate from constructor");
	(void)fixture_predicate();
}

__attribute__((visibility("default")))
int fixture_predicate(void) {
	fixture_record("predicate", "test predicate executed");
	return 7;
}
