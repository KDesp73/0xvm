#include "db.h"

/* ------------------------------------------------------------- commands */

void print_help(Debugger *db)
{
    dbg_out(db,
        "0xdb commands:\n"
        "  help, q[uit]                       show help / exit\n"
        "  c[ontinue]                         run to a breakpoint or halt\n"
        "  s[tep] [N]                         execute N instructions (default 1)\n"
        "  n[ext]                             step over CALL\n"
        "  finish | until ADDR                run to return / address\n"
        "  b[reak] ADDR|LABEL|LINE            set a breakpoint (*ADDR forces address)\n"
        "  d[elete] N|all                     remove breakpoints\n"
        "  enable N|all, disable N|all        toggle breakpoints\n"
        "  info [registers|breakpoints|symbols|stack|locals|frames|heap|source]\n"
        "  stack [N], locals [N], frames      inspect the VM stack and call frames\n"
        "  heap                               inspect allocator blocks\n"
        "  x[/8|/16|/32|/64] ADDR [N]         dump RAM\n"
        "  dis[assemble] [ADDR] [N]           disassemble around an address\n"
        "  l[ist] [LINE|ADDR]                 show source lines\n"
        "  symbols [FILTER]                   list labels and constants\n"
        "  reset                              reload the image and reset state\n"
        "\n"
        "Addresses are hex with 0x, labels, `$`, or label+$offset. A plain number\n"
        "in `break`/`list` is a source line; prefix it with `*` for an address.\n"
        "With -m MAP, line references use the Lua source map from 0xluac.\n"
        "Use -i FILE for Console input so it does not mix with debugger commands.\n");
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static void info_cmd(Debugger *db, const char *rest)
{
    char buf[256];
    char *sub = NULL;
    char *arg = NULL;
    if (rest && *rest) {
        if (strlen(rest) >= sizeof buf) {
            dbg_err("info arguments are too long");
            return;
        }
        strcpy(buf, rest);
        char *save = NULL;
        sub = strtok_r(buf, " \t", &save);
        arg = strtok_r(NULL, " \t", &save);
    }
    if (!sub || strcmp(sub, "registers") == 0 || strcmp(sub, "regs") == 0) {
        print_registers(db);
    } else if (strcmp(sub, "breakpoints") == 0 || strcmp(sub, "break") == 0) {
        print_breakpoints(db);
    } else if (strcmp(sub, "symbols") == 0) {
        print_symbols(db, arg);
    } else if (strcmp(sub, "stack") == 0) {
        print_stack(db, arg);
    } else if (strcmp(sub, "locals") == 0) {
        print_locals(db, arg);
    } else if (strcmp(sub, "frames") == 0) {
        print_frames(db);
    } else if (strcmp(sub, "heap") == 0) {
        print_heap(db);
    } else if (strcmp(sub, "source") == 0) {
        list_cmd(db, arg);
    } else {
        dbg_err("unknown info topic '%s'", sub);
    }
}

static bool cmd_is(const char *cmd, const char *full, const char *alias)
{
    return strcmp(cmd, full) == 0 || (alias && strcmp(cmd, alias) == 0);
}

void execute_line(Debugger *db, char *line)
{
    char *save = NULL;
    char *cmd = strtok_r(line, " \t\r\n", &save);
    if (!cmd || *cmd == ';') return;
    char *arg = strtok_r(NULL, "", &save);
    if (arg) arg = trim(arg);

    if (cmd_is(cmd, "help", "h") || strcmp(cmd, "?") == 0) print_help(db);
    else if (cmd_is(cmd, "quit", "q") || strcmp(cmd, "exit") == 0) db->quit = true;
    else if (cmd_is(cmd, "continue", "c") || strcmp(cmd, "cont") == 0) continue_cmd(db);
    else if (cmd_is(cmd, "step", "s")) step_cmd(db, arg);
    else if (cmd_is(cmd, "next", "n")) next_cmd(db);
    else if (strcmp(cmd, "finish") == 0 || strcmp(cmd, "fin") == 0) finish_cmd(db);
    else if (strcmp(cmd, "until") == 0) until_cmd(db, arg);
    else if (cmd_is(cmd, "break", "b") || strcmp(cmd, "br") == 0) add_breakpoint(db, arg);
    else if (cmd_is(cmd, "delete", "d") || strcmp(cmd, "del") == 0) delete_breakpoint(db, arg);
    else if (strcmp(cmd, "enable") == 0) set_break_state(db, arg, true);
    else if (strcmp(cmd, "disable") == 0) set_break_state(db, arg, false);
    else if (cmd_is(cmd, "info", "i")) info_cmd(db, arg);
    else if (cmd_is(cmd, "stack", "st") || strcmp(cmd, "dump") == 0) print_stack(db, arg);
    else if (strcmp(cmd, "locals") == 0 || strcmp(cmd, "loc") == 0) print_locals(db, arg);
    else if (strcmp(cmd, "frames") == 0 || strcmp(cmd, "fr") == 0) print_frames(db);
    else if (strcmp(cmd, "heap") == 0) print_heap(db);
    else if (cmd_is(cmd, "examine", "x") || strncmp(cmd, "x/", 2u) == 0) {
        char examine[512];
        snprintf(examine, sizeof examine, "%s%s%s", cmd,
                 arg ? " " : "", arg ? arg : "");
        examine_cmd(db, examine);
    }
    else if (cmd_is(cmd, "disassemble", "dis")) disassemble_cmd(db, arg);
    else if (cmd_is(cmd, "list", "l")) list_cmd(db, arg);
    else if (cmd_is(cmd, "symbols", "sym")) print_symbols(db, arg);
    else if (strcmp(cmd, "reset") == 0) reset_cmd(db);
    else dbg_err("unknown command '%s' (try 'help')", cmd);
}
