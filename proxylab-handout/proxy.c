#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define SBUFSIZE 32
#define NTHREADS 4
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct {
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
}sbuf_t;
static void sbuf_init(sbuf_t *sp,int n);
static void sbuf_insert(sbuf_t *sp,int item);
static int sbuf_remove(sbuf_t *sp);
sbuf_t sbuf;

static void* work(void *vargp);
static void serve(int fd);
static void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
static void read_requesthdrs(rio_t *rp,char *hostname,char *port); 
static void parse_url(char *url,char *uri,char*hostname,char*port);

struct cacheblock{
    char data[MAX_OBJECT_SIZE];
    size_t length;
    char url[MAXLINE];
    struct cacheblock*next;
    struct cacheblock*pre;
};
struct cache{
    struct cacheblock *cache_list;
    sem_t mutex;
    size_t cur_size;
    size_t max_size;
    size_t block_max_size;
};
//LRU cache
struct cache proxy_cache;
static void cache_init(struct cache*ch,size_t maxsize,size_t block_maxsize);
static int is_cached(struct cache*ch,char *url,char *data);
static void insert_blk(struct cache*ch,char *url,char *data);


int main(int argc,char **argv)
{
    int listenfd,connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    if(argc != 2){
        fprintf(stderr,"usage args port missed");
        exit(0);
    } 
    cache_init(&proxy_cache,MAX_CACHE_SIZE,MAX_OBJECT_SIZE);
    sbuf_init(&sbuf,SBUFSIZE);
    for(int i=0;i<NTHREADS;i++){
       Pthread_create(&tid,NULL,work,NULL);
    }
    listenfd = Open_listenfd(argv[1]);
    while(1){
        clientlen=sizeof(struct sockaddr_storage);
        connfd=Accept(listenfd,(SA *)&clientaddr,&clientlen);
        sbuf_insert(&sbuf,connfd);
    }
    return 0;
}

static void* work(void *vargp){
    Pthread_detach(Pthread_self());
    while(1){
      int fd=sbuf_remove(&sbuf);
      serve(fd);
    }
    return NULL;
}
static void serve(int fd){
    char buf[MAXLINE],method[MAXLINE],url[MAXLINE],version[MAXLINE];
    char uri[MAXLINE],hostname[MAXLINE],port[MAXLINE];
    rio_t rio_brws,rio_proxy;;
    Rio_readinitb(&rio_brws,fd);
    if(Rio_readlineb(&rio_brws,buf,MAXLINE)==0){
        return ;
    };
    printf("proxy recieved req \n%s\n",buf);
    sscanf(buf,"%s %s %s",method,url,version);
    sprintf(version,"HTTP/1.0"); //only use http 1.0
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }
    char data[MAX_OBJECT_SIZE];
    //已经缓存
    if(is_cached(&proxy_cache,url,data)){
        Rio_writen(fd,data,strlen(data));
        Free(data);
        return ;
    }  
    parse_url(url,uri,hostname,port);
    read_requesthdrs(&rio_brws,hostname,port);    
    int sfd=Open_clientfd(hostname,port);
    sprintf(buf,"%s %s %s\r\n",method,uri,version); 
    sprintf(buf, "%sHost: %s\r\n",buf,hostname);
    sprintf(buf, "%s%s",buf,user_agent_hdr);
    sprintf(buf, "%sConnection: close\r\n",buf);
    sprintf(buf, "%sProxy-Connection: close\r\n\r\n",buf);
    Rio_writen(sfd, buf, strlen(buf));
    size_t sz;
    Rio_readinitb(&rio_proxy,sfd);
    char *p=data;
    while((sz=Rio_readlineb(&rio_proxy,p,MAXLINE))>0){
        Rio_writen(fd,p,sz);
        p+=sz;
    }
    insert_blk(&proxy_cache,url,data);
	return;  
}
static void cache_init(struct cache*ch,size_t maxsize,size_t block_maxsize){
    ch->cache_list=NULL;
    ch->block_max_size=block_maxsize;
    ch->max_size=maxsize;
    ch->cur_size=0;
    Sem_init(&ch->mutex,0,1);
}
static int is_cached(struct cache*ch,char *url,char *data){
     P(&ch->mutex);
     struct cacheblock*p=ch->cache_list;
     if(ch->cache_list==NULL){
        V(&ch->mutex);
        return 0;
     }
     while(1){
        if(!strcmp(p->url,url)){
            if(p!=ch->cache_list){
                p->pre->next=p->next;
                p->next->pre=p;
                p->next=ch->cache_list;
                p->pre=ch->cache_list->pre;
                ch->cache_list->pre->next=p;
                ch->cache_list->pre=p;
                ch->cache_list=p;
            }
            strcpy(data,p->data);
            V(&ch->mutex);
            return 1;
        }
        p=p->next;
        if(p==ch->cache_list)break;
     }
     V(&ch->mutex);
     return 0;
}
static void insert_blk(struct cache*ch,char *url,char *data){
     P(&ch->mutex);
     size_t length=strlen(data);
     if(length>=ch->block_max_size){
        goto end;
     }
     struct cacheblock*p;
     //special judge
     if(ch->cache_list==NULL){
        p=Malloc(sizeof(struct cacheblock));
        strcpy(p->data,data);
        strcpy(p->url,url);
        p->length=length;
        p->pre=p;
        p->next=p;
        ch->cache_list=p;
        ch->cur_size+=p->length;
        goto end;
     }
     //find dup url
     p=ch->cache_list;
     while(1){
        if(!strcmp(p->url,url)){
            if(p!=ch->cache_list){
                p->pre->next=p->next;
                p->next->pre=p;
                p->next=ch->cache_list;
                p->pre=ch->cache_list->pre;
                ch->cache_list->pre->next=p;
                ch->cache_list->pre=p;
                ch->cache_list=p;
            }
            strcpy(p->data,data);
            goto end;
        }
        p=p->next;
        if(p==ch->cache_list)break;
     }
     // evict block for free space
     p=ch->cache_list->pre;
     size_t cursize=ch->cur_size;
     while(cursize+length>ch->max_size){
          cursize-=p->length;
          ch->cache_list->next=p->pre;
          p->pre->pre=ch->cache_list;
          free(p);
          p=ch->cache_list->pre;
     }
     ch->cur_size=cursize;

     //insert into cache list
     p=Malloc(sizeof(struct cacheblock));
     strcpy(p->data,data);
     strcpy(p->url,url);
     p->length=length;
     p->next=ch->cache_list;
     p->pre=ch->cache_list->pre;
     ch->cache_list->pre->next=p;
     ch->cache_list->pre=p;
     ch->cache_list=p;
     ch->cur_size+=p->length;
end:
     V(&ch->mutex);
     return;
}
/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
static void read_requesthdrs(rio_t *rp,char *hostname,char *port) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    if(strstr(buf,"Host")){
         sscanf(buf,"Host: %s",hostname);
         char *ptr=index(hostname,':');
         *ptr='\0';
         strcpy(port,ptr+1); 
    }
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
	Rio_readlineb(rp, buf, MAXLINE);
    if(strstr(buf,"Host")){
        sscanf(buf,"Host: %s",hostname);
        char *ptr=index(hostname,':');
        *ptr='\0';
        strcpy(port,ptr+1); 
    }
	printf("%s", buf);
    }
    return;
}
/* $end read_requesthdrs */

/*parse url http://www.baidu.com:80/home.html/*/
static void parse_url(char *url,char *uri,char*hostname,char *port){
    char *ptr;
    sscanf(url,"http://%s",hostname);
    ptr = index(hostname, '/');
    if (ptr) {
	    strcpy(uri, ptr);
	    *ptr = '\0';
	}
	else {
        strcpy(uri,"/");
    }
    ptr = index(hostname, ':');
    if (ptr) {
	    strcpy(port, ptr+1);
	    *ptr = '\0';
	}
	else {
        strcpy(port,"80");
    }
}
static void sbuf_init(sbuf_t *sp,int n){
    sp->buf=Calloc(n,sizeof(char *));
    sp->n=n;
    sp->front=sp->rear=0;
    Sem_init(&sp->mutex,0,1);
    Sem_init(&sp->slots,0,n);
    Sem_init(&sp->items,0,0);
}
static void sbuf_insert(sbuf_t *sp,int item){
    P(&sp->slots);
    P(&sp->mutex);
    sp->buf[(++sp->rear)%(sp->n)]=item;
    V(&sp->mutex);
    V(&sp->items);
}
static int sbuf_remove(sbuf_t *sp){
    int item;
    P(&sp->items);
    P(&sp->mutex);
    item=sp->buf[(++sp->front)%(sp->n)];
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
static void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */