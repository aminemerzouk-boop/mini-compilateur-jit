/*
 * main.c — Point d'entrée CLI de mlang
 * -----------------------------------------------------------------
 * Usage :
 *   ./mlang programme.mlang
 *   ./mlang --jit=off programme.mlang      (force l'interprétation)
 *   ./mlang --threshold=50 programme.mlang (seuil de « chaleur »)
 *   ./mlang --dump-bytecode programme.mlang
 *   ./mlang --stats programme.mlang
 *   ./mlang -e "print(1 + 2);"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ast.h"
#include "bytecode.h"
#include "compiler.h"
#include "parser.h"
#include "vm.h"

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "erreur : impossible d'ouvrir '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0)
    {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void usage(const char *prog)
{
    printf("mlang — mini-langage dynamique avec VM + JIT (projet 3)\n\n");
    printf("Usage : %s [options] fichier.mlang\n", prog);
    printf("        %s -e \"code\"\n\n", prog);
    printf("Options :\n");
    printf("  --jit=on|off       active/desactive le JIT (defaut : on)\n");
    printf("  --threshold=N      nb d'appels avant compilation (defaut : 100)\n");
    printf("  --osr-threshold=N  iterations avant OSR (defaut : 1000, 0 = off)\n");
    printf("  --dump-bytecode    affiche le bytecode genere\n");
    printf("  --stats            affiche les statistiques d'execution\n");
    printf("  --trace            affiche chaque instruction executee\n");
    printf("  -e \"code\"          execute le code donne en argument\n");
    printf("  -h, --help         cette aide\n");
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    const char *inline_src = NULL;
    int dump = 0, stats = 0, trace = 0;
    int jit = 1, threshold = 100;
    int osr_threshold = 1000;
    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else if (strncmp(a, "--jit=", 6) == 0)
            jit = strcmp(a + 6, "off") != 0;
        else if (strncmp(a, "--threshold=", 12) == 0)

            threshold = atoi(a + 12);
        else if (strncmp(a, "--osr-threshold=", 16) == 0)
            osr_threshold = atoi(a + 16);
        else if (strcmp(a, "--dump-bytecode") == 0)
            dump = 1;
        else if (strcmp(a, "--stats") == 0)
            stats = 1;
        else if (strcmp(a, "--trace") == 0)
            trace = 1;
        else if (strcmp(a, "-e") == 0 && i + 1 < argc)
            inline_src = argv[++i];
        else if (a[0] == '-')
        {
            fprintf(stderr, "option inconnue : %s\n", a);
            return 1;
        }
        else
            path = a;
    }

    char *source = NULL;
    if (inline_src)
    {
        source = malloc(strlen(inline_src) + 1);
        if (source)
            strcpy(source, inline_src);
    }
    else if (path)
    {
        source = read_file(path);
    }
    else
    {
        usage(argv[0]);
        return 1;
    }
    if (!source)
        return 1;

    /* ---------- compile : source → AST → bytecode ---------- */
    Arena arena = {0};
    Parser parser;
    Stmt *program = parse_program(&parser, &arena, source);
    if (!program)
    {
        fprintf(stderr, "erreur de syntaxe ligne %d : %s\n",
                parser.error_line, parser.error_msg);
        arena_free(&arena);
        free(source);
        return 1;
    }

    BytecodeProgram bc;
    if (compile_program(program, &bc) != 0)
    {
        arena_free(&arena);
        free(source);
        return 1;
    }

    if (dump)
        disassemble(&bc);

    /* ---------- exécution ---------- */
    VM vm;
    vm_init(&vm, &bc);
    vm.jit_enabled = jit;
    vm.jit_threshold = threshold;
    vm.osr_threshold = osr_threshold > 0 ? (uint64_t)osr_threshold : 0;
    vm.jit_verbose = 1;
    vm.trace = trace;

    double t0 = now_ms();
    int status = vm_run(&vm);
    double elapsed = now_ms() - t0;
    if (vm.has_error)
    {
        if (vm.error_line > 0)
            fprintf(stderr, "erreur d'execution (ligne %d) : %s\n",
                    vm.error_line, vm.error_msg);
        else
            fprintf(stderr, "erreur d'execution : %s\n", vm.error_msg);
    }

    /* ---------- statistiques ---------- */
    if (stats)
    {
        fprintf(stderr, "\n---------------- STATISTIQUES ----------------\n");
        fprintf(stderr, "mode JIT        : %s\n", jit ? "ON" : "OFF");
        fprintf(stderr, "seuil (appels)  : %d\n", threshold);
        fprintf(stderr, "seuil OSR       : %llu iterations\n",
                (unsigned long long)vm.osr_threshold);
        fprintf(stderr, "temps total     : %.1f ms\n", elapsed);
        fprintf(stderr, "fonctions JIT   : %d\n", vm.jit_compiled_count);
        fprintf(stderr, "temps JIT       : %.2f ms\n", vm.jit_compile_time_ms);
        fprintf(stderr, "sauts arriere   : %llu\n", (unsigned long long)vm.loop_iterations);
                int hot_shown = 0;
        for (int i = 0; i < vm.nloops; i++) {
            LoopProfile *lp = &vm.loops[i];
            if (vm.osr_threshold > 0 && lp->count >= vm.osr_threshold) {
                if (!hot_shown) {
                    fprintf(stderr, "boucles chaudes :\n");
                    hot_shown = 1;
                }
                fprintf(stderr, "   - %s @%04d : %llu iterations\n",
                        bc.funcs[lp->fn].name, lp->target,
                        (unsigned long long)lp->count);
            }
        }
        if (vm.jit_compiled_count > 0)
        {
            fprintf(stderr, "fonctions compilees :\n");
            for (int i = 0; i < bc.nfuncs; i++)
                if (vm.native[i])
                    fprintf(stderr, "   - %s (appels avant compilation : %llu)\n",
                            bc.funcs[i].name,
                            (unsigned long long)vm.call_count[i]);
        }
        fprintf(stderr, "----------------------------------------------\n");
    }

    vm_free(&vm);
    arena_free(&arena);
    free(source);
    return status;
}
