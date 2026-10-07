# Creator Info

Student: Kevius Tribble
Instructor: Dr. Jacob Sorber
CPSC 3020

# Concurrent work queue library

## always clean first

if you dont do this ar will not recreate the library from scratch it will just update it.
`make clean`

## to compile

`make`

## to use

supose you wanted to use the library compile your code like this
`gcc -o executable_name workqueuetest1.c libworkqueue.a`

example compile library and implementation code
gcc -o test  test.c libworkqueue.a
./test

## Notes

## KNOWN PROBLEMS

- A work queue can have 9999 tasks submitted over its lifetime.

## DESIGN

Files
workqueue.h -> header for libworkqueue.c comtaining important type definitions and function signatures
workqueue.c -> implementation of a workqueue & Thread pool
makefile -> makes code compilation ez

Design Choices

- Decided to use pthread_cond_init, and pthread_cond_destroy because i thought was most intuitive for them to be part of the queue struct
  becuase they are part of our queue which is dynamically allocated the static method we learned in class would throw a compiler error.
  init just makes the condition variable valid for use | [www.ibm.com/docs/en/zos/3.1.0?topic=functions-pthread-cond-init-initialize-condition-variable](https://www.ibm.com/docs/en/zos/3.1.0?topic=functions-pthread-cond-init-initialize-condition-variable)
