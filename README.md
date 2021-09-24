# BASIC interpreter

**urubasic is an interpreter for the BASIC language**

## Why BASIC?

Why not? It is an easy to learn language that is very well suited for simple tasks.

## What dialect of BASIC is it?

Based on the ECMA-55 specification from 1978, a few extensions are implemented such as:
- No support of floating points. All numeric constants and variables are 32-bit integers
- Variable names can be up 128 chars (not just single letters) long and can include the $ sign
- logical operators AND, OR and NOT are supported

## Supported operating systems and runtime environments

urubasic is supplied in source and can easily be ported to other systems. Directly supported and tested are:
- Linux 64-bit and 32-bit
- Windows Win32 (Visual Studio dsw file supplied)
- ESP8266 nonos-sdk

## Tests

The test files in the test/ directory are meant to regression test the interpreter. if you want to run the tests then you can do that only under linux. The procedure is as follows: 
1) Run once *chmod +x runtests.sh *
2) Execute the tests with *make test*
