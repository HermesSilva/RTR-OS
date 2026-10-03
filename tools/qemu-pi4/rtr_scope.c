/*
 * RTR-OS - GPIO capture device for the qemu-pi4 emulator.
 *
 * Connects to the output lines of the BCM2711 GPIO controller and writes
 * one line per transition to a character device, as it happens:
 *
 *     <virtual time in ns> <pin> <level>
 *
 * Any byte received from the character device is a request for a snapshot:
 * the current level of every captured pin is sent, so that a viewer that
 * connects later starts from a known state.
 *
 * It is the probe of the virtual oscilloscope (tools/scope/live.py).
 *
 * Live:    -chardev socket,id=scope,host=127.0.0.1,port=5555,server=on,wait=off
 *          -device rtr-scope,chardev=scope
 * To file: -chardev file,id=scope,path=scope.log -device rtr-scope,chardev=scope
 * `pins` is a bit mask of the pins to capture (all, by default).
 *
 * This file is copied into the emulator source tree by scripts/build-qemu-pi4.sh.
 * It is licensed like QEMU itself (GPL-2.0-or-later), as a QEMU device must be.
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "chardev/char-fe.h"
#include "hw/core/irq.h"
#include "hw/core/qdev.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "qom/object.h"

#define TYPE_RTR_SCOPE "rtr-scope"
OBJECT_DECLARE_SIMPLE_TYPE(RtrScopeState, RTR_SCOPE)

#define RTR_SCOPE_PINS 58
#define RTR_SCOPE_GPIO_PATH "/machine/soc/peripherals/gpio"

struct RtrScopeState {
    DeviceState parent_obj;
    CharFrontend chr;
    char *gpio_path;
    uint64_t pins;
    int level[RTR_SCOPE_PINS];
};

static void rtr_scope_emit(RtrScopeState *s, int pin, int level)
{
    char line[64];
    int size = snprintf(line, sizeof(line), "%" PRId64 " %d %d\n",
                        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), pin, level);

    qemu_chr_fe_write_all(&s->chr, (const uint8_t *)line, size);
}

static void rtr_scope_set(void *opaque, int pin, int level)
{
    RtrScopeState *s = RTR_SCOPE(opaque);

    level = level ? 1 : 0;
    if (pin < 0 || pin >= RTR_SCOPE_PINS || s->level[pin] == level) {
        return;
    }
    s->level[pin] = level;
    rtr_scope_emit(s, pin, level);
}

static int rtr_scope_can_receive(void *opaque)
{
    return 64;
}

/* Anything received is a snapshot request: current level of every captured pin. */
static void rtr_scope_receive(void *opaque, const uint8_t *buf, int size)
{
    RtrScopeState *s = RTR_SCOPE(opaque);

    for (int pin = 0; pin < RTR_SCOPE_PINS; pin++) {
        if (s->pins & (1ULL << pin)) {
            rtr_scope_emit(s, pin, s->level[pin] > 0 ? 1 : 0);
        }
    }
}

static void rtr_scope_realize(DeviceState *dev, Error **errp)
{
    RtrScopeState *s = RTR_SCOPE(dev);
    const char *path = s->gpio_path ? s->gpio_path : RTR_SCOPE_GPIO_PATH;
    Object *gpio = object_resolve_path(path, NULL);

    if (!gpio) {
        error_setg(errp, "rtr-scope: GPIO controller not found at %s", path);
        return;
    }
    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        error_setg(errp, "rtr-scope: a chardev is required");
        return;
    }

    qdev_init_gpio_in(dev, rtr_scope_set, RTR_SCOPE_PINS);
    for (int pin = 0; pin < RTR_SCOPE_PINS; pin++) {
        s->level[pin] = -1;
        if (s->pins & (1ULL << pin)) {
            qdev_connect_gpio_out(DEVICE(gpio), pin, qdev_get_gpio_in(dev, pin));
        }
    }
    qemu_chr_fe_set_handlers(&s->chr, rtr_scope_can_receive, rtr_scope_receive,
                             NULL, NULL, s, NULL, true);
}

static const Property rtr_scope_properties[] = {
    DEFINE_PROP_CHR("chardev", RtrScopeState, chr),
    DEFINE_PROP_UINT64("pins", RtrScopeState, pins, UINT64_MAX),
    DEFINE_PROP_STRING("gpio", RtrScopeState, gpio_path),
};

static void rtr_scope_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rtr_scope_realize;
    dc->desc = "RTR-OS GPIO capture (virtual oscilloscope probe)";
    device_class_set_props(dc, rtr_scope_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo rtr_scope_info = {
    .name = TYPE_RTR_SCOPE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(RtrScopeState),
    .class_init = rtr_scope_class_init,
};

static void rtr_scope_register_types(void)
{
    type_register_static(&rtr_scope_info);
}

type_init(rtr_scope_register_types)
