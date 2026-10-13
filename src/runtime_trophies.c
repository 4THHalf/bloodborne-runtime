/* Local trophy events; no trophy catalog or game assets are embedded.
 * Inspired by Supermedo/bloodborne_pc 79d44f0 (Ambrose): persist unlock events per user.
 * Our version includes title identity, edited-save gating and checked append operations. */
#define _GNU_SOURCE
#include "runtime.h"
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t trophy_lock=PTHREAD_MUTEX_INITIALIZER;
static char trophy_title[17];
void runtime_trophy_configure(const char *title) {
    pthread_mutex_lock(&trophy_lock);
    size_t n=title ? strlen(title) : 0;
    int valid=n>0 && n<sizeof(trophy_title);
    for (size_t i=0;i<n && valid;++i)
        valid=(title[i]>='A' && title[i]<='Z') || (title[i]>='0' && title[i]<='9');
    snprintf(trophy_title,sizeof(trophy_title),"%s",valid ? title : "");
    pthread_mutex_unlock(&trophy_lock);
}
static int eligible(void) {
    const char *v=getenv("BB_TROPHIES_ELIGIBLE");
    if (v && strcmp(v,"1")) return 0;
    char path[640];
    struct stat st;
    snprintf(path,sizeof(path),"%s/savedata",runtime_file_user_dir());
    if (lstat(path,&st)==0) {
        if (!S_ISDIR(st.st_mode)) return 0;
    } else if (errno!=ENOENT) return 0;
    snprintf(path,sizeof(path),"%s/savedata/.bb-edited",runtime_file_user_dir());
    if (lstat(path,&st)==0) return 0; /* Any marker, even malformed or a link, disables unlocks. */
    return errno==ENOENT;
}
int32_t runtime_trophy_unlock(int32_t id,int32_t *platinum) {
    if (platinum) *platinum=-1;
    if (id<0 || id>=128) return (int32_t)0x80551604;
    pthread_mutex_lock(&trophy_lock);
    if (!eligible()) {
        printf("Runtime: trophy %d suppressed (save ineligible)\n",id);
        pthread_mutex_unlock(&trophy_lock);
        return 0; /* Preserve the local service's success contract without granting an unlock. */
    }
    char path[640],line[96];
    snprintf(path,sizeof(path),"%s/trophies-v1.log",runtime_file_user_dir());
    time_t timestamp=time(NULL);
    int fd=-1, saved=0;
    if (trophy_title[0] && timestamp>0) {
        int n=snprintf(line,sizeof(line),"v1 %lld %s %d\n",(long long)timestamp,trophy_title,id);
        fd=open(path,O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_NONBLOCK,0600);
        struct stat st;
        if (fd>=0 && fstat(fd,&st)==0 && S_ISREG(st.st_mode)) {
            ssize_t written;
            do { written=write(fd,line,(size_t)n); } while (written<0 && errno==EINTR);
            saved=written==n;
            if (saved && fsync(fd)!=0) saved=0;
        }
        if (fd>=0) close(fd);
    }
    printf("Runtime: trophy %d %s\n",id,saved ? "unlocked (recorded locally)" : "not persisted (event write failed)");
    pthread_mutex_unlock(&trophy_lock);
    return 0;
}
#else
void runtime_trophy_configure(const char *title) { (void)title; }
int32_t runtime_trophy_unlock(int32_t id,int32_t *platinum) { (void)id; if (platinum) *platinum=-1; return 0; }
#endif
