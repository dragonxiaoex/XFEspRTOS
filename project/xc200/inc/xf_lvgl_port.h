#ifndef __XF_LVGL_PORT_H__
#define __XF_LVGL_PORT_H__

void xf_lvgl_port_init(void);
int xf_lvgl_port_lock(uint32_t timeout_ms);
void xf_lvgl_port_unlock(void);

#endif
