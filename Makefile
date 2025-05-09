
all:
	cc -o vi vi.c

clean:
	rm -f vi a.out *.log

fmt:
	clang-format --style={BasedOnStyle: llvm,UseTab: Always,IndentWidth: 8,TabWidth: 8} -i armv6-as.c

