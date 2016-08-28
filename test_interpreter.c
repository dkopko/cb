#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cb_interpreter.h"


static int
alloc_align(void    *allocator_state,
            void   **memptr,
            size_t   alignment,
            size_t   size)
{
    (void)allocator_state;

    return posix_memalign(memptr, alignment, size);
}


int
main(int argc, char **argv)
{
    static char bytecode[] = { 0 };
    struct cb_allocator       allocator;
    struct cb_process_state   process_state;
    struct cb_interpreter_arg interpreter_arg;
    struct cb_bytecode        raw_bytecode,
                              translated_bytecode;
    void                     *stack;
    size_t                    stack_len;
    int ret;

    (void)argc;
    (void)argv;

    /* Translate from portable code to threaded-interpreter code. */
    memset(&allocator, 0, sizeof(allocator));
    allocator.allocator_state = NULL;
    allocator.memalign = &alloc_align;
    memset(&raw_bytecode, 0, sizeof(raw_bytecode));
    raw_bytecode.bytecode_type = CB_BYTECODE_PORTABLE;
    raw_bytecode.bytecode      = bytecode;
    raw_bytecode.bytecode_len  = sizeof(bytecode);
    raw_bytecode.attributes    = 0;
    raw_bytecode.allocator     = NULL;
    memset(&translated_bytecode, 0, sizeof(translated_bytecode));
    memset(&interpreter_arg, 0, sizeof(interpreter_arg));
    interpreter_arg.command             = CB_INTERPRETER_TRANSLATE;
    interpreter_arg.translate.allocator = &allocator;
    interpreter_arg.translate.input     = &raw_bytecode;
    interpreter_arg.translate.output    = &translated_bytecode;
    ret = cb_interpret(&interpreter_arg);
    printf("cb_interpret() (translate): %d\n", ret);
    assert(ret == 0);
    if (ret != 0)
        return EXIT_FAILURE;

    /* Execute code. */
    stack_len = 1000;
    stack = calloc(stack_len, sizeof(uintptr_t));
    assert(stack);
    if (!stack)
        return EXIT_FAILURE;
    memset(&process_state, 0, sizeof(process_state));
    process_state.stack     = stack;
    process_state.stack_len = stack_len;
    memset(&interpreter_arg, 0, sizeof(interpreter_arg));
    interpreter_arg.command               = CB_INTERPRETER_EXECUTE;
    interpreter_arg.execute.bytecode      = &translated_bytecode;
    interpreter_arg.execute.process_state = &process_state;
    interpreter_arg.execute.step_count    = 0;
    ret = cb_interpret(&interpreter_arg);
    printf("cb_interpret() (execute): %d\n", ret);
    assert(ret == 0);
    if (ret != 0)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

