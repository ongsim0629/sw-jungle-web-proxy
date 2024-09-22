// HTTP/1.0 웹 서버 
// GET 메서드를 이용해서 정적 or 동적 콘텐츠 제공
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, int is_head);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void sigchld_handler(int sig);

// argc : 명령 줄에서 입력된 인수의 개수
// argv : 명령 줄에서 입력된 인수를 문자열로 저장하는 배열 -> argv[0]에는 항상 프로그램의 이름이 들어간다!
int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);

  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  // 인수 2개 전달 됐는 지 확인 (프로그램 이름, 포트 번호)
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // argv[1] -> 명령 줄에 입력되었던 포트 번호
  // 듣기 식별자 오픈하고 다시 리턴
  listenfd = Open_listenfd(argv[1]);
  // 무한 루프를 통해 클라이언트 요청 수락
  while (1) {
    clientlen = sizeof(clientaddr);
    // 클라이언트의 연결 요청을 수락하면 그 클라이언트와의 통신을 관리하는 파일 디스크립터
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    // 소켓 구조체 clientaddr로 부터 호스트와 서비스 이름을 받아온다.
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    // 트랜잭션 수행
    doit(connfd);
    Close(connfd);
  }
}

// 한 개의 HTTP 트랜잭션을 처리하는 함수
void doit(int fd)
{
  int is_static, is_head; 
  struct stat sbuf; 
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; 
  char filename[MAXLINE], cgiargs[MAXLINE]; 
  rio_t rio;

  // readlineb 사용하기 전에 리오 버퍼 구조체인 rio랑 fd 연결해서 초기화
  Rio_readinitb(&rio, fd);
  // 클라이언트 요청 라인을 읽고 버퍼에 저장!
  // 요청 라인의 예시 : GET /index.html HTTP/1.1
  if(!(Rio_readlineb(&rio, buf, MAXLINE))){
    return;
	}

  printf("Request headers:\n");
  printf("%s", buf);
  // 공백을 기준으로 나눠서 변수에 값을 저장한다.
  sscanf(buf, "%s %s %s", method, uri, version); 
  // method: "GET"
	// uri: "/index.html"
	// version: "HTTP/1.1"

  // strcasecmp() : 첫 번째 인자와 두 번째 인자 비교해서 같으면 0 반환
  if (strcasecmp(method, "GET") == 0)
  {
    is_head = 0;
  }
  // HEAD 메소드를 이용해서 요청한 경우
  // HEAD 메소드는 body 없이 응답 헤더만 제공하면 된다.
  else if (strcasecmp(method, "HEAD") == 0) {
    is_head = 1;
  }
  else{
    // method가 GET이나 HEAD가 아니었던 경우 => tiny web에서는 get 요청만 처리
    // main으로 돌아오고 연결을 닫은 뒤 다음 연결을 기다린다.
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  // 요청 헤더 읽고 출력 -> 특별한 처리는 하지 않는다. 
  read_requesthdrs(&rio);

  // uri를 분석해서 정적 콘텐츠인지 동적 콘텐츠인지 구분
  is_static = parse_uri(uri, filename, cgiargs);

  // 파일 존재 여부 확인
  if (stat(filename, &sbuf) < 0) 
  {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  // 정적 콘텐츠
  if (is_static) { 
    // 일반 파일인지, 읽기 권한이 있는 파일인지 확인하고 콘텐츠 제공
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, is_head); 
  }
  // 동적 콘텐츠
  else { 
    // 파일 실행 가능한지 확인하고 콘텐츠 제공
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny coundn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs); 
  }
}

// 일부 명백한 오류들을 클라이언트에게 보고하는 함수
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  // HTML 형식의 에러페이지 만들어서 body에 저장
  snprintf(body, sizeof(body), "<html><title>Tiny Error</title>");
  snprintf(body + strlen(body), sizeof(body) - strlen(body), "<body bgcolor=ffffff>\r\n");
  snprintf(body + strlen(body), sizeof(body) - strlen(body), "%s: %s\r\n", errnum, shortmsg);
  snprintf(body + strlen(body), sizeof(body) - strlen(body), "<p>%s: %s\r\n", longmsg, cause);
  snprintf(body + strlen(body), sizeof(body) - strlen(body), "<hr><em>The Tiny Web Server</em>\r\n");

  // 응답 라인 생성 -> Rio_writen을 이용해서 fd를 목적지로 하여 데이터를 씀
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  // 응답 헤더 생성 
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  // 응답 본문 전송 -> 아까 만들었던 HTML 에러 페이지를 클라이언트에게 전송
  Rio_writen(fd, body, strlen(body));
}

// 요청 헤더는 사용 안하니까 그냥 읽기만 한다.
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];
  // 요청 라인 읽기만 한다.
  Rio_readlineb(rp, buf, MAXLINE);

  // "\r\n"이 나올 때까지 요청 헤더 한 줄씩 읽어 들인다.
  while(strcmp(buf, "\r\n")){
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf); 
  }
  return;
}

// uri 분석해서 정적 콘텐츠를 요청하는지, 동적 콘텐츠를 요청하는 건지 구분한다.
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  // cgi-bin을 포함하는 모든 uri는 동적 컨텐츠를 요청한다고 가정한다.
  // uri에 cgi-bin이 포함되어있지 않다면 -> 정적 컨텐츠를 요청한다면
  if (!strstr(uri, "cgi-bin")) {
    // cgi 인자 스트링을 지운다 -> 정적 컨텐츠이기 때문에
    strcpy(cgiargs, "");
    // 파일 이름 => 현재 디렉토리 + uri
    strcpy(filename, ".");
    strcat(filename, uri);
    // uri가 /로 끝나면 기본 파일 이름 추가해주기
    if (uri[strlen(uri)-1] == '/') 
      strcat(filename, "home.html");
    return 1;
  }
  // 동적 컨텐츠 요청한다면
  // uri 예시 : /cgi-bin/adder?15000&213
  else {
    // uri에 ?가 있으면 그 이후의 문자열은 cgi 인자로 처리한다.
    // cgi 인자 15000&213
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    // cgi 인자 제외한 나머지 부분을 파일 이름으로 지정해준다
    // 파일 경로 : ./cgi-bin/adder -> cgi 프로그램의 경로
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

// 정적 콘텐츠 제공 : 주어진 파일 읽고 HTTP 응답 형태로 클라이언트에 제공 + 응답 헤더도 전송
void serve_static(int fd, char *filename, int filesize, int is_head)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF]; 
  rio_t rio;

  // 파일 이름의 접미어에서 파일 타입 얻어온다.
  get_filetype(filename, filetype);
  // 응답 라인
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  // 응답 헤더 "\r\n"이 한 줄을 끝낸다.
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnections: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

  Rio_writen(fd, buf, strlen(buf)); 
  printf("Response headers: \n");
  printf("%s", buf);

  if (is_head){
    return;
  }

  // 응답 본문 전송
  // 요청된 파일을 일단 읽기 전용으로 읽고 연결 식별자 얻어온다.
  srcfd = Open(filename, O_RDONLY, 0);                    
  // 파일을 가상 메모리 영역으로 매핑해서 효율적으로 작업 수행하도록 한다.
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  // Mmap 대신 malloc 사용
  // srcp는 할당된 메모리 포인터이고, srcfd 는 파일 디스크립터이다.
  srcp = (char *)malloc(filesize);
  if (srcp == NULL){
    fprintf(stderr, "error!");
    Close(srcfd);
    return;
  }

  // 파일 내용 읽어서 메모리로 복사 
  Rio_readn(srcfd, srcp, filesize);

  // 파일을 메모리 영역으로 매핑했으므로 이제 식별자 필요없어져서 닫는다.
  Rio_writen(fd, srcp, filesize);
  
  Close(srcfd);
  // 메모리 매핑된 파일 내용 해제 -> malloc으로 바뀌었으므로 free로 바꾸어준다.
  free(srcp);
}

// 요청된 파일의 MIME 타입을 결정한다.
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");

  else
    strcpy(filetype, "text/plain");
}

// 동적 콘텐츠 제공 : 자식 프로세스 fork하고, CGI 프로그램을 자식의 컨텍스트에서 실행한다.
void serve_dynamic(int fd, char *filename, char *cgiargs){ 
  char buf[MAXLINE], *emptylist[] = {NULL};

  // 성공을 알려주는 응답라인을 보내는 것으로 시작
  sprintf(buf, "HTTP/1.1 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf)); 
  // 응답 헤더 전송
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

	// fork()로 자식 프로세스 생성
  if (Fork() == 0) { 
    // CGI 프로그램에 환경 변수로 쿼리 문자열 전달
    setenv("QUERY_STRING", cgiargs, 1);
    // Dup2(): 파일 디스크립터 복제
    // 터미널 콘솔 등에 이루어질 출력이 이제 클라이언트랑 연결된 소켓 파일 디스크립터로 재지정 되었다.
    // 이제 모든 출력이 클라이언트로 전송된다.
    Dup2(fd, STDOUT_FILENO);
    // 자식 프로그램의 프로세스를 CGI 프로그램으로 대체한다
    Execve(filename, emptylist, environ);
  }

  // 부모 프로세스는 자식 프로세스가 종료될 때 까지 기다린다.
  // 즉, 자식 프로세스가 CGI 실행하고 결과를 클라이언트에게 전송하고 종료되어야지
  // 부모 프로세스가 이를 감지하고 다음 작업을 할 수 있다. -> 반복 실행
  wait(NULL);
}

// SIGCHLD 핸들러: 종료된 자식 프로세스를 청소
void sigchld_handler(int sig){
  // 자식 프로세스가 종료될 때마다 해당 자식의 자원을 해제
  while (waitpid(-1, NULL, WNOHANG) > 0){
  }
}
