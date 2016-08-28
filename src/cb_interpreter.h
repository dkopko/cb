#ifndef _CB_INTERP_H_
#define _CB_INTERP_H_

#include <stddef.h>
#include <stdint.h>


struct cb_allocator
{
    int (*memalign)(void    *allocator_state,
                    void   **memptr,
                    size_t   alignment,
                    size_t   size);
    void *allocator_state;
};


enum cb_interpreter_command
{
    CB_INTERPRETER_TRANSLATE,
    CB_INTERPRETER_EXECUTE
};


enum cb_bytecode_type
{
    CB_BYTECODE_PORTABLE,
    CB_BYTECODE_THREADED,
    CB_BYTECODE_NATIVE
};


struct cb_bytecode
{
    enum cb_bytecode_type  bytecode_type;
    void                  *bytecode;
    size_t                 bytecode_len;
    uint8_t                attributes;
    struct cb_allocator   *allocator;
};


struct cb_process_state
{
    uint64_t  ip;
    uint64_t  sp;
    void     *stack;
    size_t    stack_len;
};


struct cb_interpreter_arg
{
    enum cb_interpreter_command command;
    union
    {
        struct
        {
            struct cb_allocator *allocator;
            struct cb_bytecode  *input;
            struct cb_bytecode  *output;
        } translate;
        struct
        {
            struct cb_bytecode      *bytecode;
            struct cb_process_state *process_state;
            uint32_t                 step_count;
        } execute;
    };
};


int cb_interpret(struct cb_interpreter_arg *arg);


#endif /* ! defined _CB_INTERP_H_ */
