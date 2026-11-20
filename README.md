# IdaBridge

Exodus extension that lets IDA Pro debug a running Mega Drive game in Exodus.

It speaks the gRPC debugger protocol from [smd_ida_tools2](https://github.com/lab313ru/smd_ida_tools2)
by Vladimir Kononovich (Dr. MefistO). The IDA side is his [Gensida](https://github.com/lab313ru/smd_ida_tools2) plugin, unchanged.
You need it installed in IDA, see that project for its setup.
All credit for the protocol and the IDA plugin goes to him. This project only adds
Exodus as a backend in place of Gens.

## Status

Tested against IDA with the Gensida plugin and working.

Everything the Gens backend does is covered: registers, memory, VDP registers and DMA
info, PC breakpoints, read and write watchpoints, VDP breakpoints, breakpoint conditions,
pause, resume, stepping, call stack, and the executed address list IDA uses to mark code.

VRAM, CRAM and VSRAM show up in IDA at 0xD00000, 0xD10000 and 0xD20000, same as Gens.

VDP breakpoints need the memory breakpoint support added to the 315-5313 core in
interface version 2. On an older Exodus they are accepted but never fire. They also stop
at the end of the current timeslice, so the PC can be a few instructions past the access.

Breakpoint conditions are evaluated by IDA, so a conditional breakpoint that keeps
failing costs a round trip every hit. Watchpoints have the same cost, since Exodus doesn't
say which one fired and all of their conditions get checked.

While IDA is attached the 68000 trace log is switched on to build the executed address
list. That slows emulation down. Your own trace settings come back when IDA detaches.

## Building

Needs Visual Studio 2022 (Build Tools are enough) with its bundled vcpkg.

It also needs a patched Exodus. The VDP memory breakpoint support (315-5313 interface
version 2) isn't in upstream Exodus yet, and the project won't compile without it. Use
the [vdp-memory-breakpoints](https://github.com/elgroovy/Exodus/tree/vdp-memory-breakpoints)
branch of my fork, which is upstream master plus that one commit.

1. Clone this repo into `Extensions\IdaBridge` inside your Exodus source tree. The project
   uses relative paths into Exodus, so it has to live there.
2. In Visual Studio, right click the Exodus solution, then Add, Existing Project, and pick
   `IdaBridge.vcxproj`
3. Build `Release|x64`

The first build compiles gRPC through vcpkg and takes a while. After that it's cached.

## Using

Load a ROM in Exodus, then start the Gensida debugger in IDA. The bridge listens
on 127.0.0.1:23946 and IDA listens on 24946 for events, same as with Gens.

## License

GPLv2, same as smd_ida_tools2. See LICENSE.
