
# Creator Info

Student: Kevius Tribble
Instructor: Dr. Jacob Sorber
CPSC 3020

# Fat 16 parsing library

## always clean first

if you dont do this ar will not recreate the library from scratch it will just update it.
`make clean`

## to compile

`make`

## To use

supose you wanted to use the library compile your code like this
`gcc -o executable_name workqueuetest1.c libworkqueue.a`

example compile library and implementation code
gcc -o test  test.c libworkqueue.a
./test

## Notes

## KNOWN PROBLEMS

## DESIGN

Files
cat16.h -> header for cat16
cat16.c -> implementation of cat16
makefile -> makes code compilation ez

Design Choices
