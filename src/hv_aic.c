/* SPDX-License-Identifier: MIT */

#include "adt.h"
#include "aic.h"
#include "aic_regs.h"
#include "hv.h"
#include "uartproxy.h"
#include "utils.h"

#define IRQTRACE_IRQ BIT(0)

static u32 trace_hw_num[AIC_MAX_HW_NUM / 32];

static void emit_irqtrace(u16 type, u16 num)
{
    struct hv_evt_irqtrace evt = {
        .flags = IRQTRACE_IRQ,
        .type = type,
        .num = num,
    };

    hv_wdt_suspend();
    uartproxy_send_event(EVT_IRQTRACE, &evt, sizeof(evt));
    hv_wdt_resume();
}

static bool trace_aic_event(struct exc_info *ctx, u64 addr, u64 *val, bool write, int width)
{
    if (!hv_pa_rw(ctx, addr, val, write, width))
        return false;

    if (addr != (aic_base + aic_regs->event) || write || width != 2) {
        return true;
    }

    u16 type = (*val & AIC_EVENT_TYPE) >> 16;
    u16 num = *val & AIC_EVENT_NUM;

    switch (type) {
        case AIC_EVENT_TYPE_HW:
            if (trace_hw_num[num / 32] & BIT(num & 31)) {
                emit_irqtrace(type, num);
            }
            break;
        default:
            // ignore
            break;
    }

    return true;
}

bool hv_trace_irq(u32 type, u32 num, u32 count, u32 flags)
{
    dprintf("HV: hv_trace_irq type: %u start: %u num: %u flags: 0x%x\n", type, num, count, flags);
    if (type == AIC_EVENT_TYPE_HW) {
        if (num >= AIC_MAX_HW_NUM || count > AIC_MAX_HW_NUM - num) {
            printf("HV: invalid IRQ range: (%u, %u)\n", num, num + count);
            return false;
        }
        for (u32 n = num; n < num + count; n++) {
            switch (flags) {
                case IRQTRACE_IRQ:
                    trace_hw_num[n / 32] |= BIT(n & 31);
                    break;
                default:
                    trace_hw_num[n / 32] &= ~(BIT(n & 31));
                    break;
            }
        }
    } else {
        printf("HV: not handling AIC event type: 0x%02x num: %u\n", type, num);
        return false;
    }

    if (!aic_base) {
        printf("HV: AIC not initialized\n");
        return false;
    }

    static bool hooked = false;

    if (aic_base && !hooked) {
        u32 nr_hw = FIELD_GET(AIC_INFO_NR_HW, read32(aic_base + AIC_INFO));
        if (nr_hw > AIC_MAX_HW_NUM) {
            printf("HV: AIC supports more IRQs than expected! nr_hw: %u\n", nr_hw);
            return false;
        }
        hv_map_hook(aic_base, trace_aic_event, aic_regs->reg_size);
        hooked = true;
    }

    return true;
}
