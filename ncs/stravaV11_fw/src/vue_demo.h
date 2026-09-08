#ifndef VUE_DEMO_H_
#define VUE_DEMO_H_

#ifdef __cplusplus
extern "C" {
#endif

/* GFX port Phase D (display arbitration): periodically (1Hz, same cadence
 * as the earlier sensor_screen_demo.c) calls the real `vue.refresh()`
 * (Vue.cpp) and pushes its buffer to the real display via
 * gfx_demo_push_buffer() -- see vue_demo.cpp's own top-of-file note for
 * why this, not sensor_screen_demo.c's gfx_demo_show_sensors() screen
 * (never actually wired into main()'s boot sequence to begin with -- see
 * that note), is what now owns the live display. Call once, after
 * gfx_demo()'s own one-shot boot splash. */
void vue_demo_start(void);

#ifdef __cplusplus
}
#endif

#endif /* VUE_DEMO_H_ */
