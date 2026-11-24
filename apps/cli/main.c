#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "asm.h"
#include "net.h"
#include "version.h"
#define CLI_IMPLEMENTATION
#include "io/cli.h"
#include "io/logging.h"
#include "vm.h"

#define CLI_MAX_HOPS 4096u

struct CliContext {
    char* socket;
    uint16_t port;
    const char* file;
    bool trace;
    bool diff;
    bool debug;
};
typedef struct CliContext CliContext;

static CliContext ctx;

static const char *peer_host(const CliContext *c)
{
    return (c->socket && *c->socket) ? c->socket : "127.0.0.1";
}

static bool load_program(const CliContext *c, AsmProgram *program)
{
    if (!c->file) {
        ERRO("Provide a source file with -f/--file");
        return false;
    }
    if (asm_parse_file(program, c->file) != 0) {
        fprintf(stderr, "Failed to parse %s:%zu: %s\n",
                c->file, program->error_line, program->error);
        asm_program_free(program);
        return false;
    }
    INFO("Loaded %s (%zu instructions)", c->file, program->count);
    return true;
}

/* Run to the next yield/halt. With -t, log each instruction and its source line. */
static VmStatus run_program(const CliContext *c, VmState *vm,
                            const AsmProgram *program)
{
    if (!c->trace) return vm_run(vm, program->insts, program->count, 0);

    for (;;) {
        if (vm->ip >= program->count) return VM_ERR_BAD_IP;
        fprintf(stderr, "[trace ip=%u line=%zu op=%s sp=%u hp=%" PRIu64 "]\n",
                vm->ip, asm_line_of(program, vm->ip),
                vm_opcode_name(program->insts[vm->ip].op), vm->sp, vm->hp);
        VmStatus st = vm_step(vm, program->insts, program->count);
        if (st != VM_OK) return st;
    }
}

/* Run a program locally; MIGRATE is handled as a local serialize/deserialize. */
static bool execute_program(const CliContext *c, const AsmProgram *program)
{
    VmState vm;
    vm_init(&vm);
    unsigned migrations = 0;

    for (;;) {
        VmStatus st = run_program(c, &vm, program);
        if (st == VM_YIELD_MIGRATE) {
            size_t wire_len = vm_serialize(&vm, NULL, 0);
            if (wire_len == 0u) {
                ERRO("Serialization failed");
                vm_free(&vm);
                return false;
            }
            uint8_t *wire = malloc(wire_len);
            if (!wire) {
                ERRO("Out of memory");
                vm_free(&vm);
                return false;
            }
            if (vm_serialize(&vm, wire, wire_len) != wire_len ||
                vm_deserialize(&vm, wire, wire_len) != 0) {
                ERRO("Migration round trip failed");
                free(wire);
                vm_free(&vm);
                return false;
            }
            INFO("Migration #%u: ip=%u, sp=%u, fp=%u, hp=%" PRIu64
                 ", frames=%u, blocks=%u, ticks=%u (%zu bytes)",
                 ++migrations, vm.ip, vm.sp, vm.fp, vm.hp,
                 vm.fsp, vm.block_count, vm.ticks, wire_len);
            free(wire);
            continue;
        }
        INFO("Finished with status: %s (total ticks=%u)", vm_status_str(st), vm.ticks);
        bool ok = (st == VM_HALTED);
        vm_free(&vm);
        return ok;
    }
}

/* A peer sends one state, receives the next, and repeats until the VM halts.
 * `start_sending` is true for the connector and false for the listener. */
static bool migrate_peer(const CliContext *c, NetConn *conn,
                         const AsmProgram *program, bool start_sending)
{
    VmState vm, base;
    vm_init(&vm);
    vm_init(&base);
    bool have_base = false;
    bool sending = start_sending;
    bool ok = false;

    if (!start_sending) {
        INFO("Restoring initial state");
        int rc = net_recv_state(conn, &vm);
        if (rc == NET_RECV_HALTED) {
            INFO("Peer halted before the first state");
            ok = true;
            goto done;
        }
        if (rc != 0) {
            ERRO("State receive failed: %s", net_last_error());
            goto done;
        }
        have_base = (vm_clone(&vm, &base) == 0);
        sending = true;
    }

    for (unsigned hop = 0; hop < CLI_MAX_HOPS; hop++) {
        if (sending) {
            VmStatus st = run_program(c, &vm, program);
            if (st == VM_HALTED) {
                INFO("Finished with status: %s (total ticks=%u)", vm_status_str(st), vm.ticks);
                (void)net_send_halt(conn); /* release the peer from its recv */
                ok = true;
                goto done;
            }
            if (st != VM_YIELD_MIGRATE) {
                ERRO("Program stopped before MIGRATE: %s", vm_status_str(st));
                goto done;
            }
            int rc = (c->diff && have_base)
                         ? net_send_state_diff(conn, &vm, &base)
                         : net_send_state(conn, &vm);
            if (rc != 0) {
                ERRO("State transfer failed: %s", net_last_error());
                goto done;
            }
            INFO("Sent state #%u%s: ip=%u, sp=%u, hp=%" PRIu64 ", ticks=%u",
                 hop + 1u, (c->diff && have_base) ? " (diff)" : "",
                 vm.ip, vm.sp, vm.hp, vm.ticks);
            if (vm_clone(&vm, &base) != 0) { ERRO("Out of memory"); goto done; }
            have_base = true;
            sending = false;
        } else {
            int rc = (c->diff && have_base)
                         ? net_recv_state_diff(conn, &vm, &base)
                         : net_recv_state(conn, &vm);
            if (rc == NET_RECV_HALTED) {
                INFO("Peer halted");
                ok = true;
                goto done;
            }
            if (rc != 0) {
                ERRO("State receive failed: %s", net_last_error());
                goto done;
            }
            INFO("Restored state: ip=%u, sp=%u, hp=%" PRIu64 ", ticks=%u",
                 vm.ip, vm.sp, vm.hp, vm.ticks);
            if (vm_clone(&vm, &base) != 0) { ERRO("Out of memory"); goto done; }
            have_base = true;
            sending = true;
        }
    }
    ERRO("Migration hop limit reached (%u)", CLI_MAX_HOPS);

done:
    vm_free(&base);
    vm_free(&vm);
    return ok;
}

/* Source node: connect, hand off the state, and keep exchanging until halt. */
static bool migrate_source(const CliContext *c)
{
    AsmProgram program;
    if (!load_program(c, &program)) return false;

    NetConn conn;
    if (net_connect(peer_host(c), c->port, &conn) != 0) {
        ERRO("Connect failed: %s", net_last_error());
        asm_program_free(&program);
        return false;
    }
    INFO("Connected to %s:%u", peer_host(c), (unsigned)c->port);
    bool ok = migrate_peer(c, &conn, &program, true);

    net_close(&conn);
    asm_program_free(&program);
    return ok;
}

/* Destination node: accept a state and resume the same program. */
static bool migrate_destination(const CliContext *c)
{
    AsmProgram program;
    if (!load_program(c, &program)) return false;

    NetListener listener;
    if (net_listen(peer_host(c), c->port, &listener) != 0) {
        ERRO("Listen failed: %s", net_last_error());
        asm_program_free(&program);
        return false;
    }
    uint16_t port = net_listener_port(&listener);
    INFO("Listening on %s:%u", peer_host(c), (unsigned)port);

    NetConn conn;
    if (net_accept(&listener, &conn) != 0) {
        ERRO("Accept failed: %s", net_last_error());
        net_close_listener(&listener);
        asm_program_free(&program);
        return false;
    }
    net_close_listener(&listener);

    bool ok = migrate_peer(c, &conn, &program, false);
    net_close(&conn);
    asm_program_free(&program);
    return ok;
}

bool connect_handler(void* context)
{
    INFO("Connecting to destination");
    return migrate_source(context);
}

bool listen_handler(void* context)
{
    return migrate_destination(context);
}

bool run_handler(void* context)
{
    CliContext ctx = *(CliContext*)(context);
    AsmProgram program;
    if (!load_program(&ctx, &program)) return false;
    bool ok = execute_program(&ctx, &program);
    asm_program_free(&program);
    return ok;
}

int main(int argc, char** argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Prints this message", no_argument),
        cli_arg_new('v', "version", "Prints the program version", no_argument),
        cli_arg_new('S', "socket", "Peer/bind address (default 127.0.0.1)", required_argument),
        cli_arg_new('P', "port", "TCP port", required_argument),
        cli_arg_new('f', "file", "Assembly source file to run", required_argument),
        cli_arg_new('t', "trace", "Trace each instruction with its source line", no_argument),
        cli_arg_new('d', "diff", "Send page-level diffs after the first state", no_argument),
        cli_arg_new('D', "debug", "Prefix program output with vm tick numbers", no_argument),
        NULL
    );

    Dispatcher dispatcher = {0};
    dispatcher_init(&dispatcher);
    dispatcher_add(&dispatcher, "connect", "connect to a destination and migrate", connect_handler);
    dispatcher_add(&dispatcher, "listen", "listen for a migrated state and resume", listen_handler);
    dispatcher_add(&dispatcher, "run", "run a program locally", run_handler);

    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, &dispatcher, "", "Written by KDesp73 -- Experimental");
                goto cleanup;
            case 'v':
                printf("%s\n", VERSION_STRING);
                goto cleanup;
            case 'S':
                ctx.socket = optarg;
                break;
            case 'P': {
                char *end = NULL;
                errno = 0;
                unsigned long value = strtoul(optarg, &end, 10);
                if (errno != 0 || !end || *end != '\0' || value == 0u || value > 65535u) {
                    ERRO("Invalid port: %s", optarg);
                    goto cleanup;
                }
                ctx.port = (uint16_t)value;
                break;
            }
            case 'f':
                ctx.file = optarg;
                break;
            case 't':
                ctx.trace = true;
                break;
            case 'd':
                ctx.diff = true;
                break;
            case 'D':
                ctx.debug = true;
                vm_set_debug_ticks(1);
                break;
            default:
                break;
        }
    }

    if (argc <= 1) { ERRO("Please provide a command and/or argument"); exit(0); }
    char* cmd = argv[argc-1];

    HandlerFunc handler = dispatcher_find(&dispatcher, cmd);
    if (!handler) { ERRO("No such command: %s", cmd); exit(0); }
    if (!handler(&ctx)) { ERRO("Command `%s` failed", cmd); exit(0); }

cleanup:
    cli_args_free(&args);
    return 0;
}
