#ifndef STD_H
#define STD_H

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define var long
#define byte unsigned char

#include "file.h"
#include "term.h"
#include "socket.h"

var quit(void);
var print10(var n);
var print16(var n);
#define pow std__pow
var pow(var x, var y);
var flush();
var print(var txt);
var printb(var buf, var len);

struct buffer {
	var data;
	var length;
	var alloced;
};

var buffer__append(var b, var data, var len); 
var buffer__append10(var b, var n);
var str_cmp(var a, var b);
var str_dup(var a);
var std_alloc(var size);
var std_free(var data);


#ifdef _MSC_VER
void __builtin_trap();
#endif
var run(var a);
var quit(void);
#endif

