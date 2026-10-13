/* Standalone notification process. The guest thread only queues accepted unlock IDs;
 * one host worker owns spawn, pipes, geometry updates and display pacing. */
#define _GNU_SOURCE
#include "runtime.h"
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
static pthread_mutex_t notification_lock=PTHREAD_MUTEX_INITIALIZER;
static struct { int x,y,w,h,visible; } bounds;
static struct { int id; uint64_t at; char title[17]; } notices[16];
static unsigned head,count;
static int started,enabled,preview;
static char helper[4096];
static uint64_t monotonic_ms(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC,&time);
    return (uint64_t)time.tv_sec*1000+(uint64_t)time.tv_nsec/1000000;
}
void runtime_notification_window(int x,int y,int w,int h,int visible) {
    pthread_mutex_lock(&notification_lock);
    bounds.x=x; bounds.y=y; bounds.w=w; bounds.h=h; bounds.visible=visible;
    pthread_mutex_unlock(&notification_lock);
}
void runtime_notification_unlock(const char *title,int id) {
    pthread_mutex_lock(&notification_lock);
    if (enabled && count<16) {
        unsigned i=(head+count++)%16;
        notices[i].id=id; notices[i].at=monotonic_ms();
        snprintf(notices[i].title,sizeof(notices[i].title),"%s",title);
    }
    pthread_mutex_unlock(&notification_lock);
}
static int close_on_exec_pipe(int fds[2]) {
    if (pipe(fds)) return -1;
    if (fcntl(fds[0],F_SETFD,FD_CLOEXEC)<0 || fcntl(fds[1],F_SETFD,FD_CLOEXEC)<0) {
        close(fds[0]); close(fds[1]); return -1;
    }
    return 0;
}
static int send_line(int fd,const char *line) {
    size_t n=strlen(line); ssize_t done;
    do { done=write(fd,line,n); } while (done<0 && errno==EINTR);
    if (done==(ssize_t)n) return 1;
    /* All messages fit PIPE_BUF: a nonblocking pipe write is atomic. */
    return done<0 && (errno==EAGAIN || errno==EWOULDBLOCK) ? 0 : -1;
}
static void *notification_worker(void *unused) {
    (void)unused;
    sigset_t mask; sigemptyset(&mask); sigaddset(&mask,SIGPIPE);
    pthread_sigmask(SIG_BLOCK,&mask,NULL); /* A dead helper cannot terminate the game. */
    int input[2]={-1,-1},output[2]={-1,-1}; pid_t child=-1;
    posix_spawn_file_actions_t actions;
    if (close_on_exec_pipe(input) || close_on_exec_pipe(output)) goto done;
    if (posix_spawn_file_actions_init(&actions)) goto done;
    int error=posix_spawn_file_actions_adddup2(&actions,input[0],STDIN_FILENO);
    if (!error) error=posix_spawn_file_actions_adddup2(&actions,output[1],STDOUT_FILENO);
    char *args[]={helper,preview ? "--prototype" : "--runtime",NULL};
    if (!error) error=posix_spawn(&child,helper,&actions,NULL,args,environ);
    posix_spawn_file_actions_destroy(&actions);
    close(input[0]); input[0]=-1; close(output[1]); output[1]=-1;
    if (error) { fprintf(stderr,"Notifications: helper could not start (%d); unlocks remain recorded\n",error); goto done; }
    /* Wait for the webview listener before sending the first event. Startup has a fixed budget. */
    struct pollfd p={output[0],POLLIN,0};
    int ready=0; char reply[512]; size_t used=0;
    for (unsigned i=0;i<80 && !ready;++i) {
        int result=poll(&p,1,250);
        if (result<0 && errno==EINTR) continue;
        if (result<0 || (p.revents&(POLLERR|POLLNVAL))) break;
        if (result>0) {
            ssize_t n=read(output[0],reply+used,sizeof(reply)-1-used);
            if (n<=0) break;
            used+=(size_t)n; reply[used]=0;
            ready=strstr(reply,"\"type\":\"ready\"")!=NULL;
            if (used==sizeof(reply)-1 && !ready) break;
        }
    }
    if (!ready) { fprintf(stderr,"Notifications: helper did not become ready; unlocks remain recorded\n"); goto done; }
    printf("Notifications: helper ready (%s)\n",preview ? "preview" : "live");
    /* Keep output open: the helper acknowledges displays. Drain it without blocking. */
    if (fcntl(output[0],F_SETFL,O_NONBLOCK)<0 || fcntl(input[1],F_SETFL,O_NONBLOCK)<0) goto done;
    unsigned long long seq=0; uint64_t next_notice=0;
    int old_x=0,old_y=0,old_visible=-1;
    unsigned previews=0; uint64_t next_preview=0;
    const int preview_ids[]={21,7,1,0,35};
    for (;;) {
        char ignored[512];
        for (unsigned i=0;i<8 && read(output[0],ignored,sizeof(ignored))>0;++i) {}
        int status; pid_t waited=waitpid(child,&status,WNOHANG);
        if (waited==child) { child=-1; break; }
        if (waited<0 && errno!=EINTR) break;
        pthread_mutex_lock(&notification_lock);
        int x=bounds.x+(bounds.w>460 ? bounds.w-460 : 0),y=bounds.y;
        int visible=bounds.visible && bounds.w>=460 && bounds.h>=220;
        uint64_t now=monotonic_ms();
        while (count && (now<notices[head].at || now-notices[head].at>30000)) { head=(head+1)%16; --count; }
        if (preview && visible && previews<5 && now>=next_preview && count<16) {
            unsigned i=(head+count++)%16;
            notices[i].id=preview_ids[previews++]; notices[i].at=now;
            snprintf(notices[i].title,sizeof(notices[i].title),"CUSA03173");
            next_preview=now+8000;
        }
        char line[256]; int result=1;
        if (x!=old_x || y!=old_y || visible!=old_visible) {
            snprintf(line,sizeof(line),"{\"v\":1,\"seq\":%llu,\"kind\":\"window\",\"id\":0,\"x\":%d,\"y\":%d,\"duration_ms\":1000,\"visible\":%s}\n",++seq,x,y,visible ? "true" : "false");
            result=send_line(input[1],line);
            if (result==1) { old_x=x; old_y=y; old_visible=visible; }
        }
        if (result>=0 && visible && count && now>=next_notice) {
            int id=notices[head].id;
            snprintf(line,sizeof(line),"{\"v\":1,\"seq\":%llu,\"id\":%d,\"title\":\"%s\",\"x\":%d,\"y\":%d,\"duration_ms\":%d}\n",++seq,id,notices[head].title,x,y,id==0 ? 7000 : 5000);
            result=send_line(input[1],line);
            if (result==1) {
                printf("Notifications: sent trophy %d (%s)\n",id,preview ? "preview" : "live");
                head=(head+1)%16; --count; next_notice=now+(id==0 ? 8000 : 6000);
            }
        }
        pthread_mutex_unlock(&notification_lock);
        if (result<0) break;
        struct timespec delay={0,250000000}; nanosleep(&delay,NULL);
    }
done:
    for (unsigned i=0;i<2;++i) { if (input[i]>=0) close(input[i]); if (output[i]>=0) close(output[i]); }
    if (child>0) {
        /* EOF asks this owned helper to exit. Bound cleanup if it failed to initialize. */
        int status; unsigned i;
        for (i=0;i<20;++i) {
            pid_t result=waitpid(child,&status,WNOHANG);
            if (result==child || (result<0 && errno==ECHILD)) break;
            struct timespec delay={0,100000000}; nanosleep(&delay,NULL);
        }
        if (i==20) { kill(child,SIGKILL); while (waitpid(child,&status,0)<0 && errno==EINTR) {} }
    }
    pthread_mutex_lock(&notification_lock); enabled=0; count=0; pthread_mutex_unlock(&notification_lock);
    return NULL;
}
void runtime_notifications_start(void) {
    const char *path=getenv("BB_OVERLAY_HELPER"),*off=getenv("BB_TROPHY_OVERLAY");
    if (!path || path[0]!='/' || strlen(path)>=sizeof(helper) || (off && !strcmp(off,"0"))) return;
    pthread_mutex_lock(&notification_lock);
    if (!started) {
        started=1; const char *v=getenv("BB_TROPHY_OVERLAY_PREVIEW"); preview=v && !strcmp(v,"1");
        snprintf(helper,sizeof(helper),"%s",path);
        pthread_t thread; enabled=pthread_create(&thread,NULL,notification_worker,NULL)==0;
        if (enabled) pthread_detach(thread);
    }
    pthread_mutex_unlock(&notification_lock);
}
#else
void runtime_notification_window(int x,int y,int w,int h,int visible) { (void)x;(void)y;(void)w;(void)h;(void)visible; }
void runtime_notification_unlock(const char *title,int id) { (void)title;(void)id; }
void runtime_notifications_start(void) {}
#endif
