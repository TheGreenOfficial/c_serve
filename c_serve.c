#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>

#define MAX_BUFFER 8192
#define MAX_HEADERS 100
#define MAX_PATH 4096

typedef struct {
    char *key;
    char *value;
} header_t;

typedef struct {
    char method[16];
    char path[MAX_PATH];
    char version[16];
    header_t headers[MAX_HEADERS];
    int header_count;
} http_request_t;

typedef struct {
    int code;
    char *status;
    header_t headers[MAX_HEADERS];
    int header_count;
    char *body;
    size_t body_len;
} http_response_t;

typedef struct {
    char *root;
    int port;
    int daemon;
} config_t;

config_t config = {".", 8080, 0};

void sanitize_path(char *path) {
    char *p = path;
    while (*p) {
        if (*p == '.' && *(p+1) == '.' && (*(p+2) == '/' || *(p+2) == '\0')) {
            *path = '\0';
            return;
        }
        if (*p == '\\' || *p == ';' || *p == '|' || *p == '`') {
            *p = '_';
        }
        p++;
    }
}

void url_decode(char *str) {
    char *p = str, *q = str;
    while (*p) {
        if (*p == '%' && isxdigit((unsigned char)*(p+1)) && isxdigit((unsigned char)*(p+2))) {
            *q = (char) strtol(p + 1, NULL, 16);
            p += 2;
        } else if (*p == '+') {
            *q = ' ';
        } else {
            *q = *p;
        }
        p++; q++;
    }
    *q = '\0';
}

int parse_request(const char *buffer, http_request_t *req) {
    char line[MAX_BUFFER];
    const char *p = buffer;
    int line_len;
    
    req->header_count = 0;
    
    sscanf(p, "%15s %4095s %15s", req->method, req->path, req->version);
    
    p = strchr(p, '\n');
    if (!p) return -1;
    p++;
    
    while (p && *p && *p != '\r' && *p != '\n') {
        const char *end = strchr(p, '\n');
        if (!end) break;
        
        line_len = end - p;
        if (line_len >= MAX_BUFFER - 1) line_len = MAX_BUFFER - 2;
        memcpy(line, p, line_len);
        line[line_len] = '\0';
        
        char *colon = strchr(line, ':');
        if (colon && req->header_count < MAX_HEADERS - 1) {
            *colon = '\0';
            char *value = colon + 1;
            while (*value == ' ') value++;
            
            req->headers[req->header_count].key = strdup(line);
            req->headers[req->header_count].value = strdup(value);
            req->header_count++;
        }
        
        p = end + 1;
    }
    
    url_decode(req->path);
    sanitize_path(req->path);
    return 0;
}

void free_request(http_request_t *req) {
    for (int i = 0; i < req->header_count; i++) {
        free(req->headers[i].key);
        free(req->headers[i].value);
    }
}

void init_response(http_response_t *res, int code, const char *status) {
    res->code = code;
    res->status = strdup(status);
    res->header_count = 0;
    res->body = NULL;
    res->body_len = 0;
    
    time_t now = time(NULL);
    char date[64];
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
    
    res->headers[res->header_count].key = strdup("Date");
    res->headers[res->header_count].value = strdup(date);
    res->header_count++;
    
    res->headers[res->header_count].key = strdup("Server");
    res->headers[res->header_count].value = strdup("c_serve/1.0");
    res->header_count++;
}

void add_header(http_response_t *res, const char *key, const char *value) {
    if (res->header_count < MAX_HEADERS - 1) {
        res->headers[res->header_count].key = strdup(key);
        res->headers[res->header_count].value = strdup(value);
        res->header_count++;
    }
}

void set_body(http_response_t *res, const char *body, size_t len) {
    res->body = malloc(len);
    memcpy(res->body, body, len);
    res->body_len = len;
    char cl[32];
    snprintf(cl, sizeof(cl), "%zu", len);
    add_header(res, "Content-Length", cl);
}

void free_response(http_response_t *res) {
    free(res->status);
    for (int i = 0; i < res->header_count; i++) {
        free(res->headers[i].key);
        free(res->headers[i].value);
    }
    free(res->body);
}

int send_response(int client_fd, http_response_t *res) {
    char buffer[MAX_BUFFER];
    int len = snprintf(buffer, sizeof(buffer), "HTTP/1.1 %d %s\r\n", res->code, res->status);
    
    for (int i = 0; i < res->header_count; i++) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "%s: %s\r\n", 
                       res->headers[i].key, res->headers[i].value);
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "\r\n");
    
    if (write(client_fd, buffer, len) != len) return -1;
    if (res->body && write(client_fd, res->body, res->body_len) != (ssize_t)res->body_len) return -1;
    
    return 0;
}

int is_php_file(const char *path) {
    const char *ext = strrchr(path, '.');
    return ext && strcasecmp(ext, ".php") == 0;
}

int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int is_directory(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

char *find_index_file(const char *dir_path) {
    static char path[MAX_PATH];
    const char *index_files[] = {"index.php", "index.html", "index.htm", NULL};
    
    for (int i = 0; index_files[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir_path, index_files[i]);
        if (file_exists(path)) return path;
    }
    return NULL;
}

void generate_directory_listing(const char *dir_path, http_response_t *res) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    
    char buffer[8192];
    char *p = buffer;
    
    p += snprintf(p, sizeof(buffer) - (p - buffer),
                 "<html><head><title>Index of %s</title></head>"
                 "<body><h1>Index of %s</h1><ul>", dir_path, dir_path);
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0) continue;
        
        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        stat(full_path, &st);
        
        char size[32];
        if (S_ISDIR(st.st_mode)) strcpy(size, "DIR");
        else snprintf(size, sizeof(size), "%ld", st.st_size);
        
        p += snprintf(p, sizeof(buffer) - (p - buffer),
                     "<li><a href=\"%s\">%s</a> - %s</li>",
                     entry->d_name, entry->d_name, size);
    }
    
    closedir(dir);
    
    strcat(p, "</ul></body></html>");
    set_body(res, buffer, strlen(buffer));
    add_header(res, "Content-Type", "text/html");
}

int execute_php(const char *script_path, http_response_t *res) {
    int pipefd[2];
    if (pipe(pipefd) == -1) return -1;
    
    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        
        execlp("php", "php", script_path, NULL);
        exit(1);
    }
    
    close(pipefd[1]);
    
    char buffer[8192];
    size_t total = 0;
    char *output = malloc(8192);
    
    ssize_t n;
    while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        output = realloc(output, total + n + 1);
        memcpy(output + total, buffer, n);
        total += n;
    }
    
    close(pipefd[0]);
    waitpid(pid, NULL, 0);
    
    output[total] = '\0';
    set_body(res, output, total);
    add_header(res, "Content-Type", "text/html");
    
    free(output);
    return 0;
}

int serve_file(const char *file_path, http_response_t *res) {
    FILE *file = fopen(file_path, "rb");
    if (!file) return -1;
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *content = malloc(size);
    if (fread(content, 1, size, file) != (size_t)size) {
        free(content);
        fclose(file);
        return -1;
    }
    
    fclose(file);
    
    const char *content_type = "text/plain";
    const char *ext = strrchr(file_path, '.');
    if (ext) {
        if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) content_type = "text/html";
        else if (strcasecmp(ext, ".css") == 0) content_type = "text/css";
        else if (strcasecmp(ext, ".js") == 0) content_type = "application/javascript";
        else if (strcasecmp(ext, ".json") == 0) content_type = "application/json";
        else if (strcasecmp(ext, ".png") == 0) content_type = "image/png";
        else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) content_type = "image/jpeg";
        else if (strcasecmp(ext, ".gif") == 0) content_type = "image/gif";
        else if (strcasecmp(ext, ".pdf") == 0) content_type = "application/pdf";
    }
    
    set_body(res, content, size);
    add_header(res, "Content-Type", content_type);
    
    free(content);
    return 0;
}

void build_full_path(char *full_path, size_t size, const char *root, const char *path) {
    if (path[0] == '/') {
        strncpy(full_path, root, size - 1);
        full_path[size - 1] = '\0';
        strncat(full_path, path, size - strlen(full_path) - 1);
    } else {
        strncpy(full_path, root, size - 1);
        full_path[size - 1] = '\0';
        strncat(full_path, "/", size - strlen(full_path) - 1);
        strncat(full_path, path, size - strlen(full_path) - 1);
    }
}

void handle_request(int client_fd, const char *client_ip) {
    char buffer[MAX_BUFFER];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    http_request_t req;
    if (parse_request(buffer, &req) == -1) {
        close(client_fd);
        return;
    }
    
    http_response_t res;
    
    if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
        init_response(&res, 501, "Not Implemented");
        set_body(&res, "Method not implemented", 22);
        add_header(&res, "Content-Type", "text/plain");
        send_response(client_fd, &res);
        free_response(&res);
        free_request(&req);
        close(client_fd);
        return;
    }
    
    char full_path[MAX_PATH];
    build_full_path(full_path, sizeof(full_path), config.root, req.path);
    
    if (is_directory(full_path)) {
        char *index_file = find_index_file(full_path);
        if (index_file) {
            strncpy(full_path, index_file, sizeof(full_path) - 1);
            full_path[sizeof(full_path) - 1] = '\0';
        } else {
            init_response(&res, 200, "OK");
            generate_directory_listing(full_path, &res);
            send_response(client_fd, &res);
            free_response(&res);
            free_request(&req);
            close(client_fd);
            return;
        }
    }
    
    if (!file_exists(full_path)) {
        init_response(&res, 404, "Not Found");
        set_body(&res, "File not found", 14);
        add_header(&res, "Content-Type", "text/plain");
        send_response(client_fd, &res);
        free_response(&res);
        free_request(&req);
        close(client_fd);
        return;
    }
    
    init_response(&res, 200, "OK");
    
    if (is_php_file(full_path)) {
        if (execute_php(full_path, &res) == -1) {
            free_response(&res);
            init_response(&res, 500, "Internal Server Error");
            set_body(&res, "PHP execution failed", 20);
            add_header(&res, "Content-Type", "text/plain");
        }
    } else {
        if (serve_file(full_path, &res) == -1) {
            free_response(&res);
            init_response(&res, 500, "Internal Server Error");
            set_body(&res, "Failed to read file", 19);
            add_header(&res, "Content-Type", "text/plain");
        }
    }
    
    send_response(client_fd, &res);
    free_response(&res);
    free_request(&req);
    close(client_fd);
}

void signal_handler(int sig) {
    if (sig == SIGCHLD) {
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }
}

void show_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("A lightweight web server with PHP support\n\n");
    printf("Options:\n");
    printf("  -r, --root DIR    Set root directory (default: current directory)\n");
    printf("  -p, --port PORT   Set port (default: 8080)\n");
    printf("  -d, --daemon      Run as daemon\n");
    printf("  -h, --help        Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s                    # Serve current directory on port 8080\n", program_name);
    printf("  %s -r /var/www -p 80  # Serve /var/www on port 80\n", program_name);
}

int parse_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--root") == 0) {
            if (i + 1 < argc) {
                config.root = argv[++i];
            } else {
                fprintf(stderr, "Error: --root requires a directory argument\n");
                return -1;
            }
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                config.port = atoi(argv[++i]);
                if (config.port <= 0 || config.port > 65535) {
                    fprintf(stderr, "Error: Invalid port number\n");
                    return -1;
                }
            } else {
                fprintf(stderr, "Error: --port requires a port number\n");
                return -1;
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0) {
            config.daemon = 1;
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            show_usage(argv[0]);
            return -1;
        }
    }
    
    if (!is_directory(config.root)) {
        fprintf(stderr, "Error: Root directory '%s' does not exist or is not a directory\n", config.root);
        return -1;
    }
    
    return 0;
}

int create_server_socket() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 128) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }
    
    return server_fd;
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) exit(0);
    
    if (setsid() < 0) {
        perror("setsid");
        exit(1);
    }
    
    umask(0);
    
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);
}

int main(int argc, char *argv[]) {
    if (parse_arguments(argc, argv) != 0) {
        return 1;
    }
    
    if (config.daemon) {
        daemonize();
    }
    
    signal(SIGCHLD, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    int server_fd = create_server_socket();
    if (server_fd < 0) {
        return 1;
    }
    
    if (!config.daemon) {
        char hostname[256];
        gethostname(hostname, sizeof(hostname));
        printf("c_serve running on http://%s:%d\n", hostname, config.port);
        printf("Root directory: %s\n", config.root);
        printf("Press Ctrl+C to stop\n\n");
    }
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (errno != EINTR) perror("accept");
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        
        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);
            handle_request(client_fd, client_ip);
            exit(0);
        } else if (pid > 0) {
            close(client_fd);
        } else {
            perror("fork");
            close(client_fd);
        }
    }
    
    close(server_fd);
    return 0;
}
