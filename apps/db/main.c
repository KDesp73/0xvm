// SPDX-License-Identifier: MIT
#include "db.h"

#define CLI_IMPLEMENTATION
#include "io/cli.h"

/* ---------------------------------------------------------------- loading */

static bool load_program(Debugger *db, const char *path, const char *symbols_path,
                         const char *const *include_dirs, size_t include_dir_count)
{
    uint8_t *buf = NULL;
    size_t len = 0u;
    if (!tools_read_file(path, &buf, &len)) return false;

    if (tools_is_rom(buf, len)) {
        VmStatus st = vm_load_rom(&db->vm, buf, len);
        if (st != VM_OK) {
            dbg_err("cannot load ROM: %s", vm_status_str(st));
            free(buf);
            return false;
        }
        db->image_len = tools_rom_payload_len(buf, len);
        db->entry = read_be16(buf + 10u);
        db->image_end = (uint32_t)VM_IMAGE_START + (uint32_t)db->image_len;
        db->image = malloc(db->image_len);
        if (!db->image) {
            dbg_err("out of memory loading ROM");
            free(buf);
            return false;
        }
        memcpy(db->image, buf + VM_ROM_HEADER_SIZE, db->image_len);
        free(buf);

        if (symbols_path) {
            AsmProgram *program = malloc(sizeof *program);
            if (!program || !tools_assemble_file_ex(symbols_path, program,
                                                    include_dirs,
                                                    include_dir_count)) {
                free(program);
                dbg_err("cannot load symbols from '%s'", symbols_path);
                return false;
            }
            if (db->program) { asm_program_free(db->program); free(db->program); }
            db->program = program;
            if (program->len != db->image_len)
                dbg_err("symbol source layout differs from the ROM; line info may be wrong");
        }
        return true;
    }

    AsmProgram *program = malloc(sizeof *program);
    if (!program || !tools_assemble_file_ex(path, program, include_dirs,
                                            include_dir_count)) {
        free(program);
        free(buf);
        return false;
    }
    free(buf);
    db->image_len = program->len;
    db->image = malloc(db->image_len);
    if (!db->image) {
        dbg_err("out of memory loading program");
        asm_program_free(program);
        free(program);
        return false;
    }
    memcpy(db->image, program->bytes, db->image_len);
    db->entry = (uint16_t)(VM_IMAGE_START + program->entry);
    db->image_end = (uint32_t)VM_IMAGE_START + (uint32_t)db->image_len;
    db->program = program;

    VmStatus st = vm_load_image(&db->vm, db->image, db->image_len, db->entry);
    if (st != VM_OK) {
        dbg_err("cannot load image: %s", vm_status_str(st));
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Print this help message", no_argument),
        cli_arg_new('v', "version", "Print the program version", no_argument),
        cli_arg_new('f', "file", "Assembly source or VMIM ROM file", required_argument),
        cli_arg_new('s', "symbols", "Assembly source to use as symbols for a ROM", required_argument),
        cli_arg_new('m', "lua-map", "Lua source-line map (.lumap) to overlay", required_argument),
        cli_arg_new('i', "input", "File to use for Console input", required_argument),
        cli_arg_new('x', "execute", "Read debugger commands from a script", required_argument),
        cli_arg_new('I', "include-dir", "Add a directory to the INCLUDE search path", required_argument),
        NULL
    );

    const char *path = NULL;
    const char *symbols_path = NULL;
    const char *lua_map_path = NULL;
    const char *input_path = NULL;
    const char *script_path = NULL;
    const char **include_dirs = NULL;
    size_t include_dir_count = 0u;
    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, NULL, "0xdb -f FILE [-s SOURCE] [-m MAP] [-i INPUT] [-x SCRIPT] [-I DIR]...",
                         "Interactive debugger for 0xvm programs. Console input is read\n"
                         "from -i (or /dev/null) so it never competes with commands.");
                cli_args_free(&args);
                free(include_dirs);
                return EXIT_SUCCESS;
            case 'v':
                tools_print_version("0xdb");
                cli_args_free(&args);
                free(include_dirs);
                return EXIT_SUCCESS;
            case 'f': path = optarg; break;
            case 's': symbols_path = optarg; break;
            case 'm': lua_map_path = optarg; break;
            case 'i': input_path = optarg; break;
            case 'x': script_path = optarg; break;
            case 'I': {
                const char **tmp = realloc(include_dirs,
                    (include_dir_count + 1u) * sizeof *tmp);
                if (!tmp) {
                    dbg_err("out of memory");
                    cli_args_free(&args);
                    free(include_dirs);
                    return EXIT_FAILURE;
                }
                include_dirs = tmp;
                include_dirs[include_dir_count++] = optarg;
                break;
            }
            default: break;
        }
    }
    cli_args_free(&args);

    if (!path) {
        dbg_err("provide a file with -f/--file");
        free(include_dirs);
        return EXIT_FAILURE;
    }

    Debugger db;
    memset(&db, 0, sizeof db);
    db.out = stdout;
    db.status = VM_OK;
    db.program_path = path;

    vm_init(&db.vm);
    VmHost host;
    host_init(&host);
    HostStdio stdio;
    host_stdio_init(&stdio, false);
    db.program_input = input_path ? fopen(input_path, "rb") : fopen("/dev/null", "rb");
    if (!db.program_input) {
        dbg_err("cannot open Console input '%s'", input_path ? input_path : "/dev/null");
        free(include_dirs);
        return EXIT_FAILURE;
    }
    stdio.input = db.program_input;
    stdio.output = stdout;
    host_stdio_add(&host, &stdio);
    VmDevice device = host_device(&host);
    vm_set_device(&db.vm, &device);

    if (script_path) {
        db.cmd = fopen(script_path, "rb");
        if (!db.cmd) {
            dbg_err("cannot open command script '%s'", script_path);
            fclose(db.program_input);
            free(include_dirs);
            return EXIT_FAILURE;
        }
        db.interactive = false;
    } else {
        db.cmd = stdin;
        db.interactive = isatty(fileno(stdin)) != 0;
    }

    if (!load_program(&db, path, symbols_path, include_dirs, include_dir_count)) {
        if (script_path) fclose(db.cmd);
        fclose(db.program_input);
        free(db.image);
        vm_free(&db.vm);
        free(include_dirs);
        return EXIT_FAILURE;
    }
    free(include_dirs);

    if (lua_map_path && !source_map_load(&db.source_map, lua_map_path)) {
        if (script_path) fclose(db.cmd);
        fclose(db.program_input);
        if (db.program) { asm_program_free(db.program); free(db.program); }
        free(db.image);
        vm_free(&db.vm);
        return EXIT_FAILURE;
    }
    if (lua_map_path && db.source_map.count == 0u)
        dbg_err("source map '%s' is empty", lua_map_path);

    dbg_out(&db, "0xdb: loaded %s (%zu bytes)\n", path, db.image_len);
    print_current(&db);
    if (db.interactive) print_help(&db);

    char line[DBG_LINE_MAX];
    while (!db.quit) {
        if (db.interactive) {
            dbg_out(&db, "0xdb> ");
            fflush(db.out);
        }
        if (!fgets(line, sizeof line, db.cmd)) {
            if (db.interactive) dbg_out(&db, "\n");
            break;
        }
        execute_line(&db, line);
    }

    if (script_path) fclose(db.cmd);
    if (db.program_input != stdin) fclose(db.program_input);
    source_map_free(&db.source_map);
    if (db.program) { asm_program_free(db.program); free(db.program); }
    free(db.image);
    vm_free(&db.vm);
    return EXIT_SUCCESS;
}
