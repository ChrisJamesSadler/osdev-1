#include "module_stubs.h"

syscall_table *SYSCALL_TABLE;
char dbgbuf[256];

#include "ini.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

size_t strlen(const char* str)
{
	size_t ret = 0;
	while ( str[ret] != 0 )
		ret++;
	return ret;
}

char *strdup(const char *s){
	char *ret=(char*)malloc(strlen(s));
	strncpy(ret, s, strlen(s));
	return ret;
}

void dputs(void *handle, char *c){
	devwrite(handle, strlen(c), c);
}

struct config{
	char *display;
} c;

void displaywrite(const char *s){
	void *handle=devopen(c.display);
    dputs(handle, (char*)s);
    devclose(handle);
}

extern "C" int handler(void *c, const char* section, const char* name, const char* value){
	#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	dbgpf("BOOT: [%s] %s=%s\n", section, name, value);

	if(MATCH("default", "display")){
		((config*)c)->display=strdup(value);
		displaywrite("Starting BT/OS...");
	}else if(MATCH("default", "load")){
		module_load((char*)value);
	}
	return 1;
}

void boot_thread(void*){
	dbgout("BOOT: Boot manager loaded.\n");
	if (ini_parse("INIT:/config.ini", &handler, &c) < 0) {
            panic("(BOOT) Can't load 'config.ini'!\n");
    }
}

extern "C" int module_main(syscall_table *systbl){
	SYSCALL_TABLE=systbl;
	new_thread(&boot_thread, NULL);
	return 0;
}
