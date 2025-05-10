
#include "file.h"
#include "folder.h"

var file__delete(var path)
{
#ifdef _WIN32
	return DeleteFileA((char*)path) == 0;
#else
	return unlink((char*)path);
#endif
}

var file__rename(var src, var dest)
{
#ifdef _WIN32
	return MoveFileA((char*)src, (char*)dest) == 0;
#else
	return rename((char*)src, (char*)dest);
#endif
}


var file__size(var path)
{
	FILE *fp;
	var si;
	fp = fopen((char*)path, "rb");
    
	if (!fp) {
		return 0;
	}
	fseek(fp, 0, SEEK_END);
	si = ftell(fp);
	fclose(fp);
	return si;
}

var file__load(var path, var offset, var size)
{
	var buf;
	FILE *fp;
	var ret;
	fp = fopen((char*)path, "rb");
	if (!fp) {
		return 0;
	}
	buf = std_alloc(size+1);
	if (!buf) {
		return 0;
	}
	fseek(fp, offset, SEEK_SET);
	ret = fread((void*)buf, 1, size, fp);
	if (ret != size) {
		std_free(buf);
		buf = 0;
	}
	((byte*)buf)[size] = '\0';
	fclose(fp);
	return buf;
}

var file__save(var path, var offset, var buf, var size)
{
	FILE *fp;
	var ret;
	char *mode1 = "rb+";
	char *mode2 = "wb+";
	if (offset < 0) {
		mode1 = "wb";	
	}
	fp = fopen((char*)path, mode1);
	if (!fp) {
		fp = fopen((char*)path, mode2);
		if (!fp) {
			return -1;
		}
	}
	if (offset > 0) {
		if (fseek(fp, offset, SEEK_SET)) {
			fseek(fp, 0, SEEK_SET);
		}
	}
	ret = fwrite((void*)buf, 1, size, fp);
	fclose(fp);
	return ret;
}

var folder__create(var path)
{
	return mkfldr((char*)path);
}

var folder__delete(var path)
{
	return rmfldr((char*)path);
}

var folder__list(var path)
{
	FOLDER *f;
	char *entry;
	var buf[3];
	buf[0] = 0;
	f = openfldr((char*)path);
	if (!f) {
		return 0;
	}
	entry = readfldr(f);
	if (entry) {
		buffer__append((var)buf, (var)entry, strlen(entry));
	}
	while (entry) {
		entry = readfldr(f);
		if (!entry) {
			break;
		}
		buffer__append((var)buf, (var)"\n", 1);
		buffer__append((var)buf, (var)entry, strlen(entry));
	}
	closefldr(f);
	if (buf[0]) {
		((char*)buf[0])[buf[1]] = '\0';
	} else {
		return (var)strdup("");
	}
	return buf[0];
}


var file__get_home()
{
	char *p = getenv("HOME");
	if (p) {
		return (var)p;
	}
	return (var)"";
}

