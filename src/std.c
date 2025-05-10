/*
 As sun, earth, water & wind, this work is neither ours nor yours.

                  MMXXV PUBLIC DOMAIN by JML

    The authors and contributors disclaim copyright, patents 
           and all related rights to this software.

 Anyone is free to copy, modify, publish, use, compile, sell, or
 distribute this software, either in source code form or as a
 compiled binary, for any purpose, commercial or non-commercial,
 and by any means.

 The authors waive all rights to patents, both currently owned
 by the authors or acquired in the future, that are necessarily
 infringed by this software, relating to make, have made, repair,
 use, sell, import, transfer, distribute or configure hardware
 or software in finished or intermediate form, whether by run,
 manufacture, assembly, testing, compiling, processing, loading
 or applying this software or otherwise.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT OF ANY PATENT, COPYRIGHT, TRADE SECRET OR OTHER
 PROPRIETARY RIGHT.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#include "std.h"

var print10(var n)
{
	printf("%ld", n);
	return 0;

}

var print16(var n)
{
	printf("0x%lX", n);
	return 0;

}

var pow(var x, var y)
{
	var r = 1;
	if (x == 0) {
		return 0;
	}
	while (y > 0) {
		r = r * x;
		y--;
	}
	while (y < 0) {
		r = r / x;
		y++;
	}
	return r;
}

var flush()
{
	fflush(stdout);
	return 0;
}

var print(var txt)
{
	if (!txt) {
		printf("(nullptr)");
		return -1;
	}
	printf("%s", (char*)txt);
	return 0;
}

var printb(var buf, var len)
{
#ifdef _WIN32
	WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), 
			(char*)buf, len, NULL, NULL);
#else
	fwrite((void*)buf, len, 1, stdout);
#endif
	return 0;
}

var buffer__append(var b, var data, var len) 
{
	var *buf = (var*)b;
	var end;
	if (buf[0] == 0) {
		buf[0] = std_alloc(4096);
		buf[1] = 0;
		buf[2] = 4096;
	}
	end = buf[1] + len;
	if ((end+2) >= buf[2]) {
		buf[2] = end + 4096;
		buf[0] = (var)realloc((void*)buf[0], buf[2]);
	}
	memcpy((char*)(buf[0] + buf[1]), (char*)data, len);
	buf[1] = end;
	((char*)buf[0])[end] = 0;
	return 0;
}

var buffer__append10(var b, var n) 
{
	char tmp[64];
	int i = 0;
	int sub = 0;
	var max = 10000;
	var t;

	if (n == 0) {
		tmp[0] = '0';
		return buffer__append(b, (var)tmp, 1);
	}
	t = max * 10;
	while (t > max) {
		max = t;
		t = t * 10;
	}

	if (n == -n) { /* -32768 or -2147483648 or -9223372036854775808 */
		sub = 1; 
		n = n + 1;
		tmp[0] = '-';
		i = 1;
	} else if (n < 0) {
		n = -n;
		tmp[0] = '-';
		i = 1;
	}
	while (max > 1) {
		t = max / 10;
		if (n >= t) {
			break;
		}
		max = t;
	}	
	while (max > 1) {
		tmp[i] = '0' + (n / max);
		i++;
		n = (n % max) - sub;
		sub = 0;
		max = max / 10;
	}	
	return buffer__append(b, (var)tmp, i);
}

var str_cmp(var a, var b)
{
	return strcmp((void*)a, (void*)b);
}

var str_dup(var a)
{
	return (var)strdup((void*)a);
}

#ifdef _MSC_VER
void __builtin_trap()
{
    __debugbreak();
}
#endif

var run(var a)
{
	return system((char*)a);
}


var quit(void)
{
	term__deinit();
	exit(0);
	return -1;
}

var std_alloc(var size)
{
	char *v = malloc(size);
	memset(v, 0, size);
	return (var)v;
}

var std_free(var mem)
{
	free((void*)mem);
	return 0;
}

var dispose(var self_) 
{
	VIRTUAL(object, self_)->dispose(self_,
		       	(var)(VIRTUAL(object, self_)->parent));
	return std_free((var)self_);
}

/* object class */
static var _object__dispose(var self_, var parent_)
{
	DISPOSE(self_, parent_);
	print(VIRTUAL(object, self_)->cid);
	print((var)" free'd\n");
	return 0;
}

var object__new()
{
	static struct object__virtual call;
	struct object *self;
	call.parent = NULL;
	call.cid = (var)"object";
	call.dispose = _object__dispose;
	self = (void*)std_alloc(sizeof(*self));
	self->call = &call;
	return (var)self;
}
