#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

// a shim is a piece of code that sits between a a program and the libraries it uses 
// shim intercepts calls and can monitor and change behavior. 

// shiming and logging combine to form a self analyzing shell. 
void log_event(const char *event, const char *value, const char *name){
    char *(*original_getenv)(const char *) = dlsym(RTLD_NEXT, "getenv");
    char *path = original_getenv("CLEMSHLOG_PATH");
    char *pipeline = original_getenv("CLEMSH_PIPELINE");
    char *stage = original_getenv("CLEMSH_STAGE");
    if (path == NULL || pipeline == NULL || stage == NULL) return;
    int fd = open(path, O_WRONLY | O_APPEND);
    if (fd == -1) return;
    if (value == NULL){
        value = "";
    }
    dprintf(fd, "%s\t%s:%s\t%s=%s\n", event, pipeline, stage, name, value);
    close(fd);
}
char *getenv(const char *name){
    //call real get env using dlsym
    char *(*original_getenv)(const char *) = dlsym(RTLD_NEXT, "getenv");
    char *value = original_getenv(name); 

    // log helper
    log_event("ENVREAD", value, name);

    //return result of getenv
    return value;
}

int setenv(const char *name, const char *value, int overwrite){
    //call real set env using dlsym
    int (*original_setenv)(const char *, const char *, int) = dlsym(RTLD_NEXT, "setenv");
    char *(*original_getenv)(const char *) = dlsym(RTLD_NEXT, "getenv");
    char *prev_value = original_getenv(name); // see if variable was previously set
    int result = original_setenv(name, value, overwrite); 

    //log helper
    //if variable is not set and overwrite == 0 log it
    
    if (prev_value == NULL && overwrite == 0){
        log_event("ENVWRITE", "", name);
    }
    // if overwrite is not 0 log it
    
    else if (overwrite != 0){
        log_event("ENVWRITE", value, name);
    }
    // variable is set and overwrite == 0 dont log it
    else if (prev_value != NULL && overwrite == 0){return result;}
    
    //return result of setenv
    return result;
}

int putenv(char *string){
    //call real put env using dlsym
    int (*original_putenv)(char *) = dlsym(RTLD_NEXT, "putenv");
    int value = original_putenv(string); 

    // log helper
    // name value split
    char *copy = strdup(string);
    if (copy != NULL){
        char *equal_sign = strchr(copy, '=');
        if (equal_sign != NULL){
            *equal_sign = '\0'; 
            char *name = copy;
            char *value = equal_sign + 1;
            log_event("ENVWRITE", value, name);
            *equal_sign = '='; 
        }else{
        log_event("ENVWRITE", "", copy);
        }
    }
    free(copy);
    //return result of putenv
    return value;
}

int unsetenv(const char *name){
    //call real unset env using dlsym
    int (*original_unsetenv)(const char *) = dlsym(RTLD_NEXT, "unsetenv");
    int value = original_unsetenv(name); 

    // log helper
    log_event("ENVWRITE", "", name);
    //return result of unsetenv
    return value;
}