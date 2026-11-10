# Creator Info

Student: Kevius Tribble
Instructor: Dr. Jacob Sorber
CPSC 3020

# Fat 16 parsing library

## Vagrant

Start the VM from this directory with `vagrant up`. The project is synced to
`/vagrant` using rsync. To keep changes synced while you work, run
`vagrant rsync-auto` in a separate terminal and use the VM normally in the
other terminal. The initial `vagrant up` performs the first sync.

## always clean first

if you dont do this ar will not recreate the library from scratch it will just update it.
`make clean`

## to compile

`make`

## To use

supose you wanted to use the library compile your code like this
`gcc -o executable_name test.c libworkqueue.a`

example compile library and implementation code
gcc -o test  test.c libworkqueue.a
./test

## Notes

## KNOWN PROBLEMS

TBD


## DESIGN

Files
cat16.h -> header for cat16
cat16.c -> implementation of cat16
makefile -> makes code compilation ez

Design Choices

- fopen + fseek = oepn the file and then jump around with an offset of bytes. great if the file is too bing to load into vritual memory -> buffer + jump around | i think this would be easier to explain on a quiz.
- memset & memcmp = compiler would throw errors since cstrings are expected to have a null terminator
  strring functions are designed to go until a null terminator using strncpy should theoretically work but the comipler throws a a warning.
  memcpy/cmp can work any data not just strings given that you tell it the length so its great for our usecase witha. non null terminated file name

```shell
make
gcc -Wall -g -std=gnu11 test.c cat16.a -o test
./test
```
