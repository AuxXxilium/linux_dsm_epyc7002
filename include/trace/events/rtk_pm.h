#undef TRACE_SYSTEM
#define TRACE_SYSTEM rtk_pm

#if !defined(_TRACE_REALTEK_PM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_REALTEK_PM_H

#include <linux/tracepoint.h>

TRACE_EVENT(rtk_pm_event,

	TP_PROTO(const char *id, const char *event),

	TP_ARGS(id, event),

	TP_STRUCT__entry(
		__string(id, id)
		__string(event, event)
	),

	TP_fast_assign(
		__assign_str(id, id);
		__assign_str(event, event);
	),

	TP_printk("id=%s event=%s", __get_str(id), __get_str(event))
);

TRACE_EVENT(rtk_pm_reg_set,

	TP_PROTO(const char *type, int offset, int val),

	TP_ARGS(type, offset, val),

	TP_STRUCT__entry(
		__string(type, type)
		__field(int, offset)
		__field(int, val)
	),

	TP_fast_assign(
		__assign_str(type, type);
		__entry->offset = offset;
		__entry->val = val;
	),

	TP_printk("type=%s offset=%03x val=%08x",
		__get_str(type), __entry->offset, __entry->val)
);

TRACE_EVENT(rtk_pm_reg_update_bits,

	TP_PROTO(const char *type, int offset, int mask, int val),

	TP_ARGS(type, offset, mask, val),

	TP_STRUCT__entry(
		__string(type, type)
		__field(int, offset)
		__field(int, mask)
		__field(int, val)
	),

	TP_fast_assign(
		__assign_str(type, type);
		__entry->offset = offset;
		__entry->mask = mask;
		__entry->val = val;
	),

	TP_printk("type=%s offset=%03x mask=%08x val=%08x",
		__get_str(type), __entry->offset, __entry->mask,  __entry->val)
);
#endif /* _TRACE_REALTEK_PM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
