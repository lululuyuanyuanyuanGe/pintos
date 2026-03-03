1. Introduction
Welcome to Pintos. Pintos is a simple operating system framework for the 80x86 architecture. It supports kernel threads, loading and running user programs, and a file system, but it implements all of these in a very simple way. In the Pintos projects, you and your project team will strengthen its support in all three of these areas. You will also add a virtual memory implementation.

Pintos could, theoretically, run on a regular IBM-compatible PC. Unfortunately, it is impractical to supply every student a dedicated PC for use with Pintos. Therefore, we will run Pintos projects in a system simulator, that is, a program that simulates an 80x86 CPU and its peripheral devices accurately enough that unmodified operating systems and software can run under it. In class we will use the Bochs and QEMU simulators running inside a Linux Docker container. Pintos has also been tested with VMware Player.

These projects are hard and cumulative by design. They have a reputation of taking a lot of time, and deservedly so. We will do what we can to reduce the workload, such as providing a lot of support material, but there is plenty of hard work that needs to be done. We welcome your feedback. If you have suggestions on how we can reduce the unnecessary overhead of assignments, cutting them down to the important underlying issues, please let us know.

This chapter explains how to get started working with Pintos. You should read the entire chapter before you start work on any of the projects.


1.1 Getting Started
To get started, you'll have to use a machine with Docker installed. At University of Toronto Scarborough, docker is installed on the Linux lab machines.

First let's test the Pintos docker image:


 	
$ docker run --platform linux/amd64 --rm --name pintos -w /pintos/src/threads/build thierrysans/pintos pintos -q run alarm-zero
It should run Pintos and execute the command alarm-zero before quitting (option -q):


 	
squish-pty bochs -q
========================================================================
                       Bochs x86 Emulator 2.6.11
              Built from SVN snapshot on January 5, 2020
                Timestamp: Sun Jan  5 08:36:00 CET 2020========================================================================
00000000000i[      ] BXSHARE not set. using compile time default '/usr/local/share/bochs'

00000000000i[      ] reading configuration from bochsrc.txt
00000000000i[      ] installing nogui module as the Bochs GUI
00000000000i[      ] using log file bochsout.txt
PiLo hda1
Loading.............
Kernel command line: -q run alarm-zero
Pintos booting with 4,096 kB RAM...
383 pages available in kernel pool.
383 pages available in user pool.
Calibrating timer...  102,400 loops/s.
Boot complete.
Executing 'alarm-zero':
(alarm-zero) begin
(alarm-zero) PASS
(alarm-zero) end
Execution of 'alarm-zero' complete.
Timer: 61 ticks
Thread: 0 idle ticks, 65 kernel ticks, 0 user ticks
Console: 381 characters output
Keyboard: 0 keys pressed
Powering off...
========================================================================
Bochs is exiting with the following message:
[ACPI  ] ACPI control: soft power off
========================================================================
By default, the command pintos runs the Bochs simulator. You can as well choose to run the QEMU simulator:


 	
$ docker run --platform linux/amd64 --rm --name pintos -w /pintos/src/threads/build thierrysans/pintos pintos --qemu -- -q run alarm-zero
and the output should look like this:


 	
qemu-system-i386 -device isa-debug-exit -drive file=/tmp/4VOGYdFrP1.dsk,index=0,media=disk,format=raw 
-m 4 -net none -nographic -monitor null
PiLo hda1
Loading............
Kernel command line: -q run alarm-zero
Pintos booting with 3,968 kB RAM...
367 pages available in kernel pool.
367 pages available in user pool.
Calibrating timer...  417,792,000 loops/s.
Boot complete.
Executing 'alarm-zero':
(alarm-zero) begin
(alarm-zero) PASS
(alarm-zero) end
Execution of 'alarm-zero' complete.
Timer: 24 ticks
Thread: 0 idle ticks, 25 kernel ticks, 0 user ticks
Console: 385 characters output
Keyboard: 0 keys pressed
Powering off...
The Docker image thierrysans/pintos contains a modified version of the original Pintos source code. However you are not going to modify this code directly in the container, otherwise all of your changes will be lost as soon as the container shuts down.


1.1.1 Source Tree Overview
Now you can extract the source for Pintos by executing

 	
git clone https://github.com/ThierrySans/CSCC69-Pintos
Note: we have made some customization to the official Pintos distribution. So you should be only getting the source code from the above channels. In other words, do not download from other websites.

Let's take a look at what's inside. Here's the directory structure that you should see in pintos/src:


threads/
Source code for the base kernel, which you will modify starting in project 1.

userprog/
Source code for the user program loader, which you will modify starting with project 2.

vm/
An almost empty directory. You will implement virtual memory here in project 3.

filesys/
Source code for a basic file system. You will use this file system starting with project 2, but you will not modify it until project 4.

devices/
Source code for I/O device interfacing: keyboard, timer, disk, etc. You will modify the timer implementation in project 1. Otherwise you should have no need to change this code.

lib/
An implementation of a subset of the standard C library. The code in this directory is compiled into both the Pintos kernel and, starting from project 2, user programs that run under it. In both kernel code and user programs, headers in this directory can be included using the #include <...> notation. You should have little need to modify this code.

lib/kernel/
Parts of the C library that are included only in the Pintos kernel. This also includes implementations of some data types that you are free to use in your kernel code: bitmaps, doubly linked lists, and hash tables. In the kernel, headers in this directory can be included using the #include <...> notation.

lib/user/
Parts of the C library that are included only in Pintos user programs. In user programs, headers in this directory can be included using the #include <...> notation.

tests/
Tests for each project. You can modify this code if it helps you test your submission, but we will replace it with the originals before we run the tests.

examples/
Example user programs for use starting with project 2.

misc/
utils/
These files may come in handy if you decide to try working with Pintos on your own machine. Otherwise, you can ignore them.

1.1.2 Building Pintos
As the next step, build the source code supplied for the first project. First, run the docker container with a mount of your source:


 	
$ docker run --platform linux/amd64 --rm --name pintos -it -v "$(pwd):/pintos" thierrysans/pintos bash
Once in the Docker container, you should build the utils directory (first time only):


 	
$ cd /pintos/src/utils
$ make
Then, let us build the Pintos kernel for the first project (2. Project 1: Threads) inside the docker container:


 	
$ cd /pintos/src/threads
$ make
This will create a build directory under threads, populate it with a Makefile and a few subdirectories, and then build the kernel inside. The entire build should take less than 30 seconds.

Following the build, the following are the interesting files in the build directory:


Makefile
A copy of pintos/src/Makefile.build. It describes how to build the kernel. See Adding Source Files, for more information.

kernel.o
Object file for the entire kernel. This is the result of linking object files compiled from each individual kernel source file into a single object file. It contains debug information, so you can run GDB (see section E.5 GDB) or backtrace (see section E.4 Backtraces) on it.

kernel.bin
Memory image of the kernel, that is, the exact bytes loaded into memory to run the Pintos kernel. This is just kernel.o with debug information stripped out, which saves a lot of space, which in turn keeps the kernel from bumping up against a 512 kB size limit imposed by the kernel loader's design.

loader.bin
Memory image for the kernel loader, a small chunk of code written in assembly language that reads the kernel from disk into memory and starts it up. It is exactly 512 bytes long, a size fixed by the PC BIOS.
Subdirectories of build contain object files (.o) and dependency files (.d), both produced by the compiler. The dependency files tell make which source files need to be recompiled when other source or header files are changed.


1.1.3 Running Pintos
We've supplied a program for conveniently running Pintos in a simulator, called pintos. In the simplest case, you can invoke pintos as pintos argument.... Each argument is passed to the Pintos kernel for it to act on. The command pintos should be run in the directory where the kernel was built.


 	
$ cd /pintos/src/threads/build
$ pintos -q run alarm-multiple
In these arguments, run instructs the kernel to run a test and alarm-multiple is the test to run.

This command creates a bochsrc.txt file, which is needed for running Bochs, and then invoke Bochs.

By default, pintos runs the Bochs simulator but you can also run the same command with the QEMU simulator:


 	
$ pintos --qemu -- -q run alarm-multiple
You can run each simulator with a debugger (see section E.5 GDB). You can set the amount of memory to give the VM.

The Pintos kernel has commands and options other than run. These are not very interesting for now, but you can see a list of them using -h, e.g. pintos -h.


1.1.4 Debugging versus Testing
When you're debugging code, it's useful to be able to run a program twice and have it do exactly the same thing. On second and later runs, you can make new observations without having to discard or verify your old observations. This property is called "reproducibility." One of the simulators that Pintos supports, Bochs, can be set up for reproducibility, and that's the way that pintos invokes it by default.

Of course, a simulation can only be reproducible from one run to the next if its input is the same each time. For simulating an entire computer, as we do, this means that every part of the computer must be the same. For example, you must use the same command-line argument, the same disks, the same version of Bochs, and you must not hit any keys on the keyboard (because you could not be sure to hit them at exactly the same point each time) during the runs.

While reproducibility is useful for debugging, it is a problem for testing thread synchronization, an important part of most of the projects. In particular, when Bochs is set up for reproducibility, timer interrupts will come at perfectly reproducible points, and therefore so will thread switches. That means that running the same test several times doesn't give you any greater confidence in your code's correctness than does running it only once.

So, to make your code easier to test, we've added a feature, called "jitter," to Bochs, that makes timer interrupts come at random intervals, but in a perfectly predictable way. In particular, if you invoke pintos with the option -j seed, timer interrupts will come at irregularly spaced intervals. Within a single seed value, execution will still be reproducible, but timer behavior will change as seed is varied. Thus, for the highest degree of confidence you should test your code with many seed values.

On the other hand, when Bochs runs in reproducible mode, timings are not realistic, meaning that a "one-second" delay may be much shorter or even much longer than one second. You can invoke pintos with a different option, -r, to set up Bochs for realistic timings, in which a one-second delay should take approximately one second of real time. Simulation in real-time mode is not reproducible, and options -j and -r are mutually exclusive.

The QEMU simulator is available as an alternative to Bochs (use --qemu when invoking pintos). The QEMU simulator is much faster than Bochs, but it only supports real-time simulation and does not have a reproducible mode.


1.2 Grading
We will grade your assignments based on test results and design quality, each of which comprises 50% of your grade.


1.2.1 Testing
Your test result grade will be based on our tests. Each project has several tests, each of which has a name beginning with tests. To completely test your submission, invoke make check from the project build directory. This will build and run each test and print a "pass" or "fail" message for each one. When a test fails, make check also prints some details of the reason for failure. After running all the tests, make check also prints a summary of the test results.

For project 1, the tests will probably run faster in Bochs. For the rest of the projects, they will run much faster in QEMU. make check will select the faster simulator by default, but you can override its choice by specifying SIMULATOR=--bochs or SIMULATOR=--qemu on the make command line.

You can also run individual tests one at a time. A given test t writes its output to t.output, then a script scores the output as "pass" or "fail" and writes the verdict to t.result. To run and grade a single test, make the .result file explicitly from the build directory, e.g. make tests/threads/alarm-multiple.result. If make says that the test result is up-to-date, but you want to re-run it anyway, either run make clean or delete the .output file by hand.

By default, each test provides feedback only at completion, not during its run. If you prefer, you can observe the progress of each test by specifying VERBOSE=1 on the make command line, as in make check VERBOSE=1. You can also provide arbitrary options to the pintos run by the tests with PINTOSOPTS='...', e.g. make check PINTOSOPTS='-j 1' to select a jitter value of 1 (see section 1.1.4 Debugging versus Testing).

All of the tests and related files are in pintos/src/tests. Before we test your submission, we will replace the contents of that directory by a pristine, unmodified copy, to ensure that the correct tests are used. Thus, you can modify some of the tests if that helps in debugging, but we will run the originals.

All software has bugs, so some of our tests may be flawed. If you think a test failure is a bug in the test, not a bug in your code, please point it out. We will look at it and fix it if necessary.

Please don't try to take advantage of our generosity in giving out our test suite. Your code has to work properly in the general case, not just for the test cases we supply. For example, it would be unacceptable to explicitly base the kernel's behavior on the name of the running test case. Such attempts to side-step the test cases will receive no credit. If you think your solution may be in a gray area here, please ask us about it.


1.2.2 Design
We will judge your design based on the design document and the source code that you submit. We will read your entire design document and much of your source code.

Don't forget that design quality, including the design document, is 50% of your project grade. It is better to spend one or two hours writing a good design document than it is to spend that time getting the last 5% of the points for tests and then trying to rush through writing the design document in the last 15 minutes.


1.2.2.1 Design Document
We provide a design document template for each project. For each significant part of a project, the template asks questions in four areas:


Data Structures
The instructions for this section are always the same:


Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration. Identify the purpose of each in 25 words or less.
The first part is mechanical. Just copy new or modified declarations into the design document, to highlight for us the actual changes to data structures. Each declaration should include the comment that should accompany it in the source code (see below).

We also ask for a very brief description of the purpose of each new or changed data structure. The limit of 25 words or less is a guideline intended to save your time and avoid duplication with later areas.


Algorithms
This is where you tell us how your code works, through questions that probe your understanding of your code. We might not be able to easily figure it out from the code, because many creative solutions exist for most OS problems. Help us out a little.

Your answers should be at a level below the high level description of requirements given in the assignment. We have read the assignment too, so it is unnecessary to repeat or rephrase what is stated there. On the other hand, your answers should be at a level above the low level of the code itself. Don't give a line-by-line run-down of what your code does. Instead, use your answers to explain how your code works to implement the requirements.


Synchronization
An operating system kernel is a complex, multithreaded program, in which synchronizing multiple threads can be difficult. This section asks about how you chose to synchronize this particular type of activity.


Rationale
Whereas the other sections primarily ask "what" and "how," the rationale section concentrates on "why." This is where we ask you to justify some design decisions, by explaining why the choices you made are better than alternatives. You may be able to state these in terms of time and space complexity, which can be made as rough or informal arguments (formal language or proofs are unnecessary).

An incomplete, evasive, or non-responsive design document or one that strays from the template without good reason may be penalized. Incorrect capitalization, punctuation, spelling, or grammar can also cost points. See section D. Project Documentation, for a sample design document for a fictitious project.


1.2.2.2 Source Code
Your design will also be judged by looking at your source code. We will typically look at the differences between the original Pintos source tree and your submission, based on the output of a command like diff -urpb pintos.orig pintos.submitted. We will try to match up your description of the design with the code submitted. Important discrepancies between the description and the actual code will be penalized, as will be any bugs we find by spot checks.

The most important aspects of source code design are those that specifically relate to the operating system issues at stake in the project. For example, the organization of an inode is an important part of file system design, so in the file system project a poorly designed inode would lose points. Other issues are much less important. For example, multiple Pintos design problems call for a "priority queue," that is, a dynamic collection from which the minimum (or maximum) item can quickly be extracted. Fast priority queues can be implemented many ways, but we do not expect you to build a fancy data structure even if it might improve performance. Instead, you are welcome to use a linked list (and Pintos even provides one with convenient functions for sorting and finding minimums and maximums).

Pintos is written in a consistent style. Make your additions and modifications in existing Pintos source files blend in, not stick out. In new source files, adopt the existing Pintos style by preference, but make your code self-consistent at the very least. There should not be a patchwork of different styles that makes it obvious that three different people wrote the code. Use horizontal and vertical white space to make code readable. Add a brief comment on every structure, structure member, global or static variable, typedef, enumeration, and function definition. Update existing comments as you modify code. Don't comment out or use the preprocessor to ignore blocks of code (instead, remove it entirely). Use assertions to document key invariants. Decompose code into functions for clarity. Code that is difficult to understand because it violates these or other "common sense" software engineering practices will be penalized.

In the end, remember your audience. Code is written primarily to be read by humans. It has to be acceptable to the compiler too, but the compiler doesn't care about how it looks or how well it is written.