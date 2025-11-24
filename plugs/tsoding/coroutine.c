#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <sys/mman.h>

#include "coroutine.h"

#define COROUTINE_DA_INIT_CAP 256
// Append an item to a dynamic array
#define coroutine_da_append(da, item)                                                          \
    do {                                                                             \
        if ((da)->count >= (da)->capacity) {                                         \
            (da)->capacity = (da)->capacity == 0 ? COROUTINE_DA_INIT_CAP : (da)->capacity*2;   \
            (da)->items = realloc((da)->items, (da)->capacity*sizeof(*(da)->items)); \
            assert((da)->items != NULL && "Buy more RAM lol");                       \
        }                                                                            \
                                                                                     \
        (da)->items[(da)->count++] = (item);                                         \
    } while (0)
#define COROUTINE_UNREACHABLE(message) do { fprintf(stderr, "%s:%d: UNREACHABLE: %s\n", __FILE__, __LINE__, message); abort(); } while(0)

Stack *coroutine_init(void)
{
    Stack *stack = malloc(sizeof(*stack));
    memset(stack, 0, sizeof(*stack));

    Coroutine *c = malloc(sizeof(*c));
    memset(c, 0, sizeof(*c));
    coroutine_da_append(stack, c);

    return stack;
}

void coroutine_deinit(Stack *stack)
{
    assert(stack->count == 1);
    free(stack->items[0]);
    free(stack->items);
    free(stack);
}

void __attribute__((naked)) coroutine_restore_context(void *rsp)
{
    // @arch
    (void)rsp;
    asm(
    "    movq %rdi, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rbx\n"
    "    popq %rbp\n"
    "    popq %rsi\n"
    "    popq %rdi\n"
    "    ret\n");
}

void coroutine_pop_frame(Stack *stack, void *rsp)
{
    assert(stack->count > 0);
    stack->items[stack->count - 1]->rsp = rsp;
    stack->count -= 1;
    assert(stack->count > 0);
    coroutine_restore_context(stack->items[stack->count - 1]->rsp);
}

void __attribute__((naked)) coroutine__finish_current(void)
{
    register Stack *stack = NULL;
    asm("popq %0" : "=r" (stack));
    assert(stack->count > 0);
    stack->items[stack->count-1]->finished = true;
    coroutine_yield(stack);
    COROUTINE_UNREACHABLE("Finished coroutine was resumed!!!");
}

Coroutine *coroutine_create(Stack *stack, void (*f)(Stack*, void*), void *arg)
{
    Coroutine *c = malloc(sizeof(*c));
    memset(c, 0, sizeof(*c));
    c->stack_base = mmap(NULL, STACK_CAPACITY, PROT_WRITE|PROT_READ, MAP_PRIVATE|MAP_STACK|MAP_ANONYMOUS|MAP_GROWSDOWN, -1, 0);
    assert(c->stack_base != MAP_FAILED);
    void **rsp = (void**)((char*)c->stack_base + STACK_CAPACITY);
    // @arch
    *(--rsp) = 0;     // pad
    *(--rsp) = stack;
    *(--rsp) = coroutine__finish_current;
    *(--rsp) = f;
    *(--rsp) = stack; // push rdi
    *(--rsp) = arg;   // push rsi
    *(--rsp) = 0;     // push rbx
    *(--rsp) = 0;     // push rbp
    *(--rsp) = 0;     // push r12
    *(--rsp) = 0;     // push r13
    *(--rsp) = 0;     // push r14
    *(--rsp) = 0;     // push r15
    c->rsp = rsp;

    return c;
}

void coroutine_destroy(Coroutine *c)
{
    munmap(c->stack_base, STACK_CAPACITY);
    free(c);
}

void __attribute__((naked)) coroutine_resume(Stack *stack, Coroutine *c)
{
    (void) stack;
    (void) c;
    // @arch
    asm(
    "    pushq %rdi\n"
    "    pushq %rsi\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    movq %rsp, %rdx\n"
    "    jmp coroutine_push_frame\n");
}

void coroutine_push_frame(Stack *stack, Coroutine *c, void *rsp)
{
    assert(stack->count > 0);
    stack->items[stack->count - 1]->rsp = rsp;

    coroutine_da_append(stack, c);
    coroutine_restore_context(c->rsp);
}

void __attribute__((naked)) coroutine_yield(Stack *stack)
{
    (void) stack;
    // @arch
    asm(
    "    pushq %rdi\n"
    "    pushq %rsi\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    movq %rsp, %rsi\n"     // rsp
    "    jmp coroutine_pop_frame\n");
}
