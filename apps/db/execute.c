// SPDX-License-Identifier: MIT
#include "db.h"

/* -------------------------------------------------------------- execution */

static void stopped(Debugger *db, const char *reason)
{
    dbg_out(db, "Stopped: %s (ticks=%" PRIu64 ")\n", reason, db->vm.ticks);
    print_current(db);
}

static void run_until(Debugger *db, RunMode mode, uint16_t until,
                      uint16_t base_fsp)
{
    uint64_t steps = 0u;
    while (steps < DBG_MAX_RUN_STEPS) {
        VmStatus st = vm_step(&db->vm);
        db->status = st;
        if (st != VM_OK) {
            stopped(db, vm_status_str(st));
            return;
        }
        steps++;
        if (mode == RUN_UNTIL && db->vm.ip == until) {
            stopped(db, "reached target address");
            return;
        }
        if (mode == RUN_FINISH && db->vm.fsp <= base_fsp) {
            stopped(db, "returned from frame");
            return;
        }
        if (hit_breakpoint(db)) {
            stopped(db, "breakpoint");
            return;
        }
    }
    stopped(db, "step limit reached");
}

void step_cmd(Debugger *db, const char *arg)
{
    unsigned long count = 1u;
    if (arg && *arg) {
        char *end = NULL;
        count = strtoul(arg, &end, 10);
        if (end == arg || *end != '\0' || count == 0u) {
            dbg_err("invalid step count '%s'", arg);
            return;
        }
        if (count > 100000u) count = 100000u;
    }
    if (db->status == VM_HALTED) {
        dbg_out(db, "Program is halted; use `reset` to run it again.\n");
        return;
    }
    for (unsigned long i = 0u; i < count; i++) {
        VmStatus st = vm_step(&db->vm);
        db->status = st;
        if (st != VM_OK) {
            stopped(db, vm_status_str(st));
            return;
        }
        if (hit_breakpoint(db)) {
            stopped(db, "breakpoint");
            return;
        }
    }
    print_current(db);
}

void next_cmd(Debugger *db)
{
    if (db->status == VM_HALTED) {
        dbg_out(db, "Program is halted; use `reset` to run it again.\n");
        return;
    }
    uint8_t op = db->vm.mem[db->vm.ip];
    if ((op == OP_CALL || op == OP_CALL_I) && db->vm.fsp < VM_FRAME_COUNT) {
        run_until(db, RUN_FINISH, 0u, db->vm.fsp);
    } else {
        step_cmd(db, NULL);
    }
}

void finish_cmd(Debugger *db)
{
    if (db->vm.fsp == 0u) {
        dbg_out(db, "No call frame to finish.\n");
        return;
    }
    run_until(db, RUN_FINISH, 0u, db->vm.fsp);
}

void continue_cmd(Debugger *db)
{
    if (db->status == VM_HALTED) {
        dbg_out(db, "Program is halted; use `reset` to run it again.\n");
        return;
    }
    run_until(db, RUN_CONTINUE, 0u, 0u);
}

void until_cmd(Debugger *db, const char *arg)
{
    uint16_t addr;
    if (!arg || !resolve_expr(db, arg, &addr)) {
        dbg_err("until requires an address expression");
        return;
    }
    run_until(db, RUN_UNTIL, addr, 0u);
}

void reset_cmd(Debugger *db)
{
    VmDevice device = db->vm.dev;
    vm_init(&db->vm);
    vm_set_device(&db->vm, &device);
    db->status = vm_load_image(&db->vm, db->image, db->image_len, db->entry);
    if (db->status != VM_OK) {
        dbg_err("reset failed: %s", vm_status_str(db->status));
        return;
    }
    dbg_out(db, "Reset to entry point.\n");
    print_current(db);
}
