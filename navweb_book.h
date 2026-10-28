/* SPDX-License-Identifier: MIT
 * Private implementation included by navweb.c after the HTTP/query helpers.
 * Run the existing builder with argv, never a shell. The astronomy engine in
 * the web process remains available while the independent builder works.
 */
#ifndef NAVWEB_BOOK_H
#define NAVWEB_BOOK_H

static struct {
    char builder[PATH_MAX], directory[PATH_MAX], path[PATH_MAX];
    char filename[80], id[16], stage[512], error[512], line[1024];
    const char *ephemeris;
    pid_t pid;
    int reader, running, done, percent;
    size_t used;
} web_book = { .reader = -1 };

static void web_book_init(const char *program,const char *ephemeris){
    char executable[PATH_MAX], resolved[PATH_MAX], *slash;
    executable[0]=0;
#ifdef __APPLE__
    { uint32_t size=sizeof executable;
      if(_NSGetExecutablePath(executable,&size)) executable[0]=0; }
#elif defined(__linux__)
    { ssize_t size=readlink("/proc/self/exe",executable,sizeof executable-1);
      if(size>0) executable[size]=0; }
#endif
    if(!executable[0]) copy_n(executable,sizeof executable,program);
    if(realpath(executable,resolved)){
        slash=strrchr(resolved,'/');
        if(slash) *slash=0;
        if(strlen(resolved)+13<sizeof web_book.builder)
            snprintf(web_book.builder,sizeof web_book.builder,"%s/navalm-book",resolved);
    }
    web_book.ephemeris=ephemeris;
}

static int web_book_header(const char *req){
    const char *p=strchr(req,'\n');
    while(p && p[1] && p[1]!='\r' && p[1]!='\n'){
        const char *end=strchr(++p,'\n'), *value;
        if(!end) break;
        if((size_t)(end-p)>=16 && !strncasecmp(p,"X-Navalm-Book:",14)){
            value=p+14;
            while(value<end && (*value==' ' || *value=='\t')) value++;
            if(value<end && *value++=='1'){
                while(value<end && (*value==' ' || *value=='\t' || *value=='\r')) value++;
                if(value==end) return 1;
            }
        }
        p=end;
    }
    return 0;
}

static void web_book_line(void){
    int pct;
    char stage[512];
    web_book.line[web_book.used]=0;
    if(sscanf(web_book.line,"%d%% %511[^\n]",&pct,stage)==2){
        web_book.percent=pct<0?0:(pct>99?99:pct);
        copy_n(web_book.stage,sizeof web_book.stage,stage);
    } else if(!strncmp(web_book.line,"Book: ",6))
        copy_n(web_book.error,sizeof web_book.error,web_book.line+6);
    web_book.used=0;
}

static void web_book_drain(void){
    char data[1024];
    ssize_t n;
    if(web_book.reader<0) return;
    while((n=read(web_book.reader,data,sizeof data))>0){
        ssize_t i;
        for(i=0;i<n;i++){
            if(data[i]=='\n') web_book_line();
            else if(web_book.used+1<sizeof web_book.line)
                web_book.line[web_book.used++]=data[i];
        }
    }
}

static void web_book_poll(void){
    int status;
    pid_t result;
    struct stat info;
    if(!web_book.running) return;
    web_book_drain();
    do { result=waitpid(web_book.pid,&status,WNOHANG); } while(result<0 && errno==EINTR);
    if(result==0) return;
    web_book_drain();
    if(web_book.used) web_book_line();
    close(web_book.reader); web_book.reader=-1;
    web_book.running=0;
    if(result>0 && WIFEXITED(status) && WEXITSTATUS(status)==0 &&
       !stat(web_book.path,&info) && S_ISREG(info.st_mode) && info.st_size>0){
        web_book.done=1; web_book.percent=100;
        copy_n(web_book.stage,sizeof web_book.stage,"Saved");
    } else {
        if(!web_book.error[0]) copy_n(web_book.error,sizeof web_book.error,
            "Book generation stopped before a complete file was saved.");
        /* Failed generation never replaces a previous book. */
        rmdir(web_book.directory);
    }
}

static void web_book_status(int fd){
    char json[16384];
    Json j={json,0,sizeof json};
    jput(&j,"{");
    jstr(&j,"state",web_book.running?"running":web_book.done?"done":web_book.error[0]?"error":"idle");
    jstr(&j,"id",web_book.id); jstr(&j,"path",web_book.done?web_book.path:"");
    jstr(&j,"filename",web_book.filename); jstr(&j,"stage",web_book.stage);
    jstr(&j,"error",web_book.error); jint(&j,"percent",web_book.percent);
    jint(&j,"available",web_book.builder[0] && !access(web_book.builder,X_OK));
    jstr(&j,"engine",nav_engine_name());
    jtrim(&j); jput(&j,"}");
    send_response(fd,"200 OK","application/json",json,j.n);
}

static int web_book_bad(int fd,const char *status,const char *message){
    char json[2048];
    Json j={json,0,sizeof json};
    jput(&j,"{"); jstr(&j,"error",message); jtrim(&j); jput(&j,"}");
    send_response(fd,status,"application/json",json,j.n);
    return 1;
}

static int web_book_start(int fd,const char *qs){
    static const char *keys[]={"sun_moon","planets","stars","events","increments","corrections"};
    static char *omit[]={"--no-sun-moon","--no-planets","--no-stars","--no-events","--no-increments","--no-corrections"};
    char value[64], year[16], format[16], reduction[16];
    char directory[PATH_MAX], path[PATH_MAX], filename[80];
    char *args[32], *end;
    const char *home=getenv("HOME");
    int included[6], shade=0, selected=0, i, count=0, pipefd[2];
    long y;
    if(web_book.running) return web_book_bad(fd,"409 Conflict","A book is already being generated.");
    if(!web_book.builder[0] || access(web_book.builder,X_OK))
        return web_book_bad(fd,"503 Service Unavailable","Build navalm-book and keep it beside navalm (make navalm-book).");
    if(!qs_get(qs,"year",value,sizeof value) || !value[0])
        return web_book_bad(fd,"400 Bad Request","Choose a year from 1600 to 2200.");
    y=strtol(value,&end,10);
    if(*end || y<1600 || y>2200)
        return web_book_bad(fd,"400 Bad Request","Choose a whole year from 1600 to 2200.");
    snprintf(year,sizeof year,"%ld",y);
    copy_n(format,sizeof format,"pdf"); qs_get(qs,"format",format,sizeof format);
    if(strcmp(format,"pdf") && strcmp(format,"latex"))
        return web_book_bad(fd,"400 Bad Request","Choose PDF or LaTeX source.");
    copy_n(reduction,sizeof reduction,"both"); qs_get(qs,"reduction",reduction,sizeof reduction);
    if(strcmp(reduction,"both") && strcmp(reduction,"ageton") &&
       strcmp(reduction,"formulas") && strcmp(reduction,"none"))
        return web_book_bad(fd,"400 Bad Request","Choose a valid reduction section.");
    for(i=0;i<7;i++){
        int on=i<6;
        if(qs_get(qs,i<6?keys[i]:"shade",value,sizeof value)){
            if(strcmp(value,"0") && strcmp(value,"1"))
                return web_book_bad(fd,"400 Bad Request","Section options must be 0 or 1.");
            on=value[0]=='1';
        }
        if(i<6){ included[i]=on; selected+=on; } else shade=on;
    }
    if(!selected && !strcmp(reduction,"none"))
        return web_book_bad(fd,"400 Bad Request","Select at least one book section.");
    if(!home || home[0]!='/' || strlen(home)>sizeof web_book.directory-80)
        return web_book_bad(fd,"500 Internal Server Error","A writable absolute HOME directory is required.");
    snprintf(directory,sizeof directory,"%s/NAVALM-%s-XXXXXX",home,year);
    if(!mkdtemp(directory))
        return web_book_bad(fd,"500 Internal Server Error","Cannot create a book folder in your home directory.");
    snprintf(filename,sizeof filename,"NAVALM-%s.%s",year,!strcmp(format,"pdf")?"pdf":"tex");
    snprintf(path,sizeof path,"%s/%s",directory,filename);
    args[count++]=web_book.builder;
    args[count++]="--year"; args[count++]=year;
    args[count++]="--output"; args[count++]=path;
    args[count++]="--format"; args[count++]=format;
    args[count++]="--engine"; args[count++]=(char*)nav_engine_name();
    if(web_book.ephemeris){ args[count++]="--ephemeris"; args[count++]=(char*)web_book.ephemeris; }
    args[count++]="--reduction"; args[count++]=reduction;
    for(i=0;i<6;i++) if(!included[i]) args[count++]=omit[i];
    if(shade) args[count++]="--shade-rows";
    args[count]=NULL;
    if(pipe(pipefd)){
        rmdir(directory);
        return web_book_bad(fd,"500 Internal Server Error","Cannot start the book builder.");
    }
    fcntl(pipefd[0],F_SETFD,FD_CLOEXEC);
    fcntl(pipefd[0],F_SETFL,O_NONBLOCK);
    web_book.pid=fork();
    if(web_book.pid==0){
        close(pipefd[0]);
        if(dup2(pipefd[1],STDERR_FILENO)<0) _exit(127);
        close(pipefd[1]);
        execv(web_book.builder,args);
        perror("Book: Cannot launch navalm-book"); _exit(127);
    }
    close(pipefd[1]);
    if(web_book.pid<0){
        close(pipefd[0]); rmdir(directory);
        return web_book_bad(fd,"500 Internal Server Error","Cannot start the book builder.");
    }
    web_book.reader=pipefd[0]; web_book.running=1; web_book.done=0;
    copy_n(web_book.directory,sizeof web_book.directory,directory);
    copy_n(web_book.path,sizeof web_book.path,path);
    copy_n(web_book.filename,sizeof web_book.filename,filename);
    copy_n(web_book.id,sizeof web_book.id,strrchr(directory,'-')+1);
    web_book.percent=0; web_book.used=0; web_book.error[0]=0;
    copy_n(web_book.stage,sizeof web_book.stage,"Starting");
    return 0;
}

static void web_book_download(int fd,const char *qs){
    char id[32], header[512], data[16384];
    struct stat info;
    FILE *file;
    size_t n;
    int len;
    web_book_poll();
    if(!web_book.done || !qs_get(qs,"id",id,sizeof id) || strcmp(id,web_book.id)){
        const char *message="No completed book for this link.\n";
        send_response(fd,"404 Not Found","text/plain",message,strlen(message)); return;
    }
    file=fopen(web_book.path,"rb");
    if(!file || fstat(fileno(file),&info) || !S_ISREG(info.st_mode)){
        if(file) fclose(file);
        const char *message="Saved book is no longer available.\n";
        send_response(fd,"404 Not Found","text/plain",message,strlen(message)); return;
    }
    len=snprintf(header,sizeof header,
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\nCache-Control: no-store\r\n"
        "Connection: close\r\n\r\n",strstr(web_book.filename,".pdf")?"application/pdf":"application/x-tex",
        (long long)info.st_size,web_book.filename);
    send_all(fd,header,(size_t)len);
    while((n=fread(data,1,sizeof data,file))) send_all(fd,data,n);
    fclose(file);
}
#endif
