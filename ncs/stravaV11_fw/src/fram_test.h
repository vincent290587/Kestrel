#ifndef FRAM_TEST_H_
#define FRAM_TEST_H_

/* One-shot raw FRAM (mb85rcxx) write/read round trip at FRAM_DEMO_TEST_OFFSET
 * (implemented in main.c, next to storage_demo()). Not run automatically at
 * boot -- same reasoning as disk_raw_test.h's "SD"/"NOR" tests: this is a
 * deliberate diagnostic, and running any raw write unconditionally on every
 * boot is a foot-gun if the offset ever gets reused for something real
 * (already bit UserSettings once, at offset 0 -- see FRAM_DEMO_TEST_OFFSET's
 * own comment in main.c). Gated behind cmd_console.c's "FRAM TEST" command
 * instead. */
void fram_test_start(void);

#endif /* FRAM_TEST_H_ */
