#ifndef __XF_TASK_COM_DEF_H__
#define __XF_TASK_COM_DEF_H__

#include "FreeRTOSConfig.h"

typedef enum {
    TASK_PRIORITY_IDLE = 0,                                 /* lowest, special for idle task */
    /* User task priority begin, please define your task priority at this interval */
    TASK_PRIORITY_LOW,                                      /* low */
    TASK_PRIORITY_BELOW_NORMAL,                             /* below normal */
    TASK_PRIORITY_NORMAL,                                   /* normal */
    TASK_PRIORITY_ABOVE_NORMAL,                             /* above normal */
    TASK_PRIORITY_HIGH,                                     /* high */
    TASK_PRIORITY_SOFT_REALTIME,                            /* soft real time */
    TASK_PRIORITY_HARD_REALTIME,                            /* hard real time */
    /* User task priority end */
    /*Be careful, the max-priority number can not be bigger than configMAX_PRIORITIES - 1, or kernel will crash!!! */
    TASK_PRIORITY_TIMER = configMAX_PRIORITIES - 1,         /* highest, special for timer task to keep time accuracy */
} task_priority_type_t;

#define XF_CLI_TASK_NAME                    "xf_cli"
#define XF_CLI_TASK_STACKSIZE               (1 * 1024)
#define XF_CLI_TASK_PRIO                    TASK_PRIORITY_LOW

#define XF_NVDM_TASK_NAME                   "xf_nvdm"
#define XF_NVDM_TASK_STACKSIZE              (256)
#define XF_NVDM_TASK_PRIO                   TASK_PRIORITY_NORMAL

#define XF_BUTTON_TASK_NAME                 "xf_button"
#define XF_BUTTON_TASK_STACKSIZE            (256)
#define XF_BUTTON_TASK_PRIO                 TASK_PRIORITY_NORMAL

#define XF_POWER_TASK_NAME                  "xf_power"
#define XF_POWER_TASK_STACKSIZE             (256)
#define XF_POWER_TASK_PRIO                  TASK_PRIORITY_NORMAL

#endif
