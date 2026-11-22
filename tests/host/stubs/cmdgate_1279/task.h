/* Host stand-in: counts critical-section nesting so a test can assert the
 * gate never leaves one open. */
#ifndef TASK_H_STUB_1279
#define TASK_H_STUB_1279
extern int g_crit_depth_1279;
#define taskENTER_CRITICAL() (g_crit_depth_1279++)
#define taskEXIT_CRITICAL()  (g_crit_depth_1279--)
#endif
