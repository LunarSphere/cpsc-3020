# Creator Info
Student: Kevius Tribble
Instructor: Dr. Jacob Sorber

# Shell command parser for project 1

A small simplified command-line parser (written in C) for CPSC 3020.

It supports:
* commands with arguments
* single- and double-quoted arguments
* `<` and `>` file redirection
* pipelines using `|`
* TCP redirection targets such as `tcp:localhost:9000`
* bracketed IPv6 targets such as `tcp:[::1]:9000`


## to build it
make

## to clean up
make clean

## run in interactive mode
./clemsh.c

## to run in batch mode
create a .txt file with commands separated as newlines

./clemsh.c {filename}.txt

## Notes
This parser only builds a Pipeline data structure that represents the command pipeline typed in the by the user. It does not execute commands, create sockets, open files, or set up pipes for you. You have to do that part.

The demo.c program is included for testing and to show you how to call the parser from your code.

The tests.sh shell script is used by the Makefile to test the parser. It might also be useful to look at what it's doing as you think about how to implement testing of your own.

For this project you do **not** need to change the parser at all. Just use it. Your shell code should just link to it. Your shell should only call the functions and use the types that are visible in parser.h.


## KNOWN PROBLEMS
explain any known errors or limiations to the code 
- file pathing is relative
- on the current autograder submission shims score 0/0 despite appearing to create and populate a clemshlog.txt file
- I think my error handling is pretty robust but it consisitently fails the autograder
- my error handling strat is to just check every system or lib call and attatch a stderr message to it if its a point of failure. 
- I not sure what to do about ctrl-c as mentioned in the document. I feel like ctrl-c should quit the program


## DESIGN
Explain repo structure

parser.c/h -> code provided by Dr. Sorber to abstract input parsing | Thank god.
envshim.so -> generates during compilation | this is a shared object combined with LD preload lets system loader load the shim before other librarires | enables us to intercept commands with SHIMS
envshim.c -> Intercepts environment system calls and logs them | self analyzing shell part
clemsh.c -> the shell | doing plumbing for the system calls and pipes | got me feelin like a mario brother yahooo | your not funny -_-




# cpsc-3020
