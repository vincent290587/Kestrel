/*
 * GFX port Phase D: definitions for the globals vue_global.h/menu_host.h
 * declare -- see those headers' own comments for why `vue` (ZephyrGFX)
 * and `menu` (Menuable) are separate globals from gfx_demo.cpp's own
 * private `gfx` object.
 *
 * Deliberately NOT yet wired into main()'s boot sequence or pushed to the
 * real display -- gfx_demo.cpp's own gfx object already owns the one
 * physical LS027 buffer that's actively driven by sensor_screen_demo.c's
 * 1Hz redraw loop; making the menu actually appear on real glass needs
 * real display-arbitration design (pause that redraw while the menu is
 * open, push through the same display_write() pipeline) plus real GPIO
 * button wiring -- neither exists yet. This just defines the globals so
 * the menu system compiles and its real callback wiring (menu_content.cpp)
 * can be exercised once that integration work happens, likely as part of
 * porting Vue itself (see the GFX port plan in todo.md).
 */

#include "vue_global.h"
#include "menu_host.h"

ZephyrGFX vue;
MenuHost menu;
