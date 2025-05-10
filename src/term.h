
#ifndef TERM_H
#define TERM_H
#include "std.h"

struct term {
	var handler;
	var width;
	var height;
	var evt_type;
	var evt_length;
	var evt_data;
};

var term__new(var self);
var term__width(var self);
var term__height(var self);
var term__wait(var self, var timeout);
var term__dispose(var self);
var term__deinit();
var clipboard__set(var txt, var len);
var clipboard__get();

#endif

