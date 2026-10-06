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

As of the 10/5/25 Submission their are no known issues with project 2. lets see if the compiler says otherwise.

## DESIGN

workqueue.h -> header for libworkqueue.c comtaining important type definitions and function signatures
workqueue.c -> implementation of a workqueue & Thread pool
makefile -> makes code compilation ez
