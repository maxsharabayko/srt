/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Lewis Kirkaldie - Cinegy GmbH
 *****************************************************************************/

/*
Automatic generatation of bindings via SWIG (http://www.swig.org)
Install swig via the following (or use instructions from the link above):
   sudo apt install swig / nuget install swigwintools    
Generate the bindings using:
   mkdir srtcore/bindings/csharp -p
   swig -v -csharp -namespace SrtSharp -outdir ./srtcore/bindings/csharp/ ./srtcore/srt.i
You can now reference the SrtSharp lib in your .Net Core projects.  Ensure the srtlib.so (or srt.dll) is in the binary path of your .NetCore project.
*/

%module srt
%{
   #include "srt.h"
%}

//%include <arrays_csharp.i>

//push anything with an argument name 'buf' back to being an array (e.g. csharp defaults this type to string, which is not ideal here)
//%apply unsigned char INOUT[]  { char* buf}

/// 
/// C# related configration section, customizing binding for this language  
/// 

// add top-level code to module file, which allows C# bindings of specific objects to be injected for easier use in C# 

//%pragma(csharp) moduleimports=%{ 
//using System;
//using System.Runtime.InteropServices;

// sockaddr_in layout in C# - for easier creation of socket object from C# code
//[StructLayout(LayoutKind.Sequential)]
//public struct sockaddr_in
//{
//   public short sin_family;
//   public ushort sin_port;
//   public uint sin_addr;
//   public long sin_zero;
//};
//%}

/// Rebind objects from the default mappings for types and objects that are optimized for C#

//enums in C# are int by default, this override pushes this enum to the require uint format
//%typemap(csbase) SRT_EPOLL_OPT "uint"

//the SRT_ERRNO enum references itself another enum - we must import this other enum into the class file for resolution
//%typemap(csimports) SRT_ERRNO %{
//    using static CodeMajor;
//    using static CodeMinor;
// %}

//SWIG_CSBODY_PROXY(public, public, SWIGTYPE)
//SWIG_CSBODY_TYPEWRAPPER(public, public, public, SWIGTYPE)

///
/// General interface definition of wrapper - pull in some constants and methods.
/// Hopefully will see if instead it is possible to just re-include srt.h rather than import each element
/// 

/*
static const SRTSOCKET SRT_INVALID_SOCK = -1;
static const int SRT_ERROR = -1;

int srt_startup(void);

SRTSOCKET srt_socket (int af, int type, int protocol);
 
int srt_connect (SRTSOCKET u, const struct sockaddr* name, int namelen);
int srt_recvmsg (SRTSOCKET u, char* buf, int len);
*/
#define _SWIG
%include "srt.h";
