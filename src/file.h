#ifndef FILE_H
#define FILE_H

#include "std.h"
#include "folder.h"

var file__delete(var path);
var file__rename(var src, var dest);
var file__size(var path);
var file__load(var path, var offset, var size);
var file__save(var path, var offset, var buf, var size);
var folder__create(var path);
var folder__delete(var path);
var folder__list(var path);
var file__get_home();

#endif /* FILE_H */
