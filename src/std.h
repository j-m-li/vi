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
#define peek(v,o) (((byte*)(v))[o])
#define poke(v,o,d) ((byte*)(v))[o] = d
var str_cmp(var a, var b);
var str_dup(var a);
var std_alloc(var size);
var std_free(var data);
var run(var a);
var quit(void);

struct buffer {
	var data;
	var length;
	var alloced;
};
var buffer__append(var b, var data, var len); 
var buffer__append10(var b, var n);

/* classes */
#define VIRTUAL(cls,s) ((struct cls##__virtual*) (((struct cls*)s)->call))
#define DISPOSE(self, parent_) if (parent_) { \
		((struct object__virtual*)(parent_))->dispose(self, \
			((var)((struct object__virtual*)(parent_))->parent)); \
	}
var dispose(var self);
#define OBJECT__VIRTUAL \
	var cid; \
	struct object__virtual *parent; \
	var (*dispose)(var self, var parent)
#define OBJECT__CLASS \
	struct object__virtual *call
struct object__virtual {
	OBJECT__VIRTUAL;
};
struct object {
	OBJECT__CLASS;
};
var object__new();


#ifdef _MSC_VER
void __builtin_trap();
#endif

#endif

