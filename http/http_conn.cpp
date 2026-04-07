#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>
#include <nlohmann/json.hpp>          // JSON 库
#include "model_inference.h"          // 模型推理封装类

using json = nlohmann::json;

// ========== Session 管理 ==========
#include <unordered_map>
#include <random>
#include <string>

class SessionManager {
public:
    static std::string create_session(const std::string& username) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        const char* hex = "0123456789abcdef";
        std::string token(32, '0');
        for (int i = 0; i < 32; ++i) {
            token[i] = hex[dis(gen)];
        }
        lock_guard.lock();
        sessions_[token] = username;
        lock_guard.unlock();
        return token;
    }

    static bool verify_session(const std::string& token, std::string& username) {
        lock_guard.lock();
        auto it = sessions_.find(token);
        bool found = (it != sessions_.end());
        if (found) username = it->second;
        lock_guard.unlock();
        return found;
    }

    static void remove_session(const std::string& token) {
        lock_guard.lock();
        sessions_.erase(token);
        lock_guard.unlock();
    }

private:
    static std::unordered_map<std::string, std::string> sessions_;
    static locker lock_guard;
};

std::unordered_map<std::string, std::string> SessionManager::sessions_;
locker SessionManager::lock_guard;

// ========== 模型全局变量 ==========
static ModelInference* classifier = nullptr;
static std::vector<ModelInference*> generators(8, nullptr);

void init_models() {
    classifier = new ModelInference("models/classifier.pt");
    for (int i = 0; i < 8; ++i) {
        std::string path = "models/generator_" + std::to_string(i) + ".pt";
        generators[i] = new ModelInference(path);
    }
}

// ========== 辅助函数（原有）==========
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// ========== 全局变量（原有）==========
int http_conn::m_epollfd = -1;
std::atomic<int> http_conn::m_user_count(0);
locker m_lock;
map<string, string> users;

void http_conn::initmysql_result(connection_pool *connPool) {
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);
    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }
    MYSQL_RES *result = mysql_store_result(mysql);
    if (result == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(mysql));
        return;
    }
    int num_fields = mysql_num_fields(result);
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// ========== http_conn 成员函数实现 ==========
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname) {
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;
    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());
    init();
}

void http_conn::init() {
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;
    m_response_json.clear();
    m_response_csv.clear();
    m_cookie.clear();
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    if (0 == m_TRIGMode) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;
        if (bytes_read <= 0)
            return false;
        return true;
    } else {
        while (true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            } else if (bytes_read == 0) {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    m_url = strpbrk(text, " \t");
    if (!m_url)
        return BAD_REQUEST;
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    // if (strlen(m_url) == 1) // 不再自动添加 judge.html
    //     strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
            m_linger = true;
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else if (strncasecmp(text, "Cookie:", 7) == 0) {
        text += 7;
        text += strspn(text, " \t");
        m_cookie = text;
    } else {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE:
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            case CHECK_STATE_HEADER:
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) return BAD_REQUEST;
                else if (ret == GET_REQUEST) return do_request();
                break;
            case CHECK_STATE_CONTENT:
                ret = parse_content(text);
                if (ret == GET_REQUEST) return do_request();
                line_status = LINE_OPEN;
                break;
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

// 核心业务逻辑（已添加调试输出）
http_conn::HTTP_CODE http_conn::do_request() {

    // 1. 登录验证（对根路径和 /diagnose 接口）
    if (cgi == 0 && (strcmp(m_url, "/") == 0 || strcmp(m_url, "/diagnose") == 0)) {
        std::string username;
        bool valid = false;
        size_t pos = m_cookie.find("session_id=");
        if (pos != std::string::npos) {
            size_t end = m_cookie.find(";", pos);
            std::string token = m_cookie.substr(pos + 11, end - pos - 11);
            valid = SessionManager::verify_session(token, username);
        }
        if (!valid) {
            // 未登录，重定向到登录页
            add_status_line(302, "Found");
            add_response("Location: /log.html\r\n");
            add_blank_line();
            return REDIRECT_REQUEST;
        }
        // 登录验证通过后，如果是根路径，返回诊断页面
        if (strcmp(m_url, "/") == 0) {
            strcpy(m_real_file, doc_root);
            strcat(m_real_file, "/diagnose.html");
            if (stat(m_real_file, &m_file_stat) < 0)
                return NO_RESOURCE;
            if (!(m_file_stat.st_mode & S_IROTH))
                return FORBIDDEN_REQUEST;
            int fd = open(m_real_file, O_RDONLY);
            m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
            return FILE_REQUEST;
        }
    }

    // 2. 诊断接口（POST /diagnose）
    if (cgi == 1 && strcmp(m_url, "/diagnose") == 0) {
        try {
            json req = json::parse(m_string);
            // 情况1：用户上传了 CSV 文件
            if (req.contains("csv_data")) {
                
                std::string csv_content = req["csv_data"].get<std::string>();
                int num_samples = req.value("num_samples", 1);
                if (num_samples <= 0) num_samples = 1;

                // 解析 CSV 为样本（每64行一个样本）
                std::vector<std::vector<float>> samples;
                std::istringstream csv_stream(csv_content);
                std::string line;
                std::vector<float> current_sample;
                int row_count = 0;
                while (std::getline(csv_stream, line)) {
                    if (line.empty()) continue;
                    std::istringstream line_stream(line);
                    std::string cell;
                    std::vector<float> row;
                    while (std::getline(line_stream, cell, ',')) {
                        row.push_back(std::stof(cell));
                    }
                    if (row.size() != 3) {
                        json resp;
                        resp["status"] = "error";
                        resp["message"] = "CSV 每行应为 3 个数值";
                        m_response_json = resp.dump();
                        return DIAGNOSE_REQUEST;
                    }

                    current_sample.insert(current_sample.end(), row.begin(), row.end());
                    row_count++;
                    if (row_count == 64) {
                        samples.push_back(current_sample);
                        current_sample.clear();
                        row_count = 0;
                    }
                }
                // 丢弃不完整的最后一个样本
                if (!current_sample.empty()) {
                    // 可选记录日志
                }

                if (samples.empty()) {
                    json resp;
                    resp["status"] = "error";
                    resp["message"] = "CSV 中没有完整样本（至少需要64行）";
                    m_response_json = resp.dump();
                    return DIAGNOSE_REQUEST;
                }

                // 用第一个样本进行分类
                std::vector<float> first_sample = samples[0];

                std::vector<float> class_probs = classifier->predict(first_sample, 64, 3);
                int pred_class = std::max_element(class_probs.begin(), class_probs.end()) - class_probs.begin();
                if (pred_class < 0 || pred_class >= 8) pred_class = 0;

                // 选择对应的生成模型
                ModelInference* generator = generators[pred_class];
                fflush(stdout);

                if (!generator) {
                    json resp;
                    resp["status"] = "error";
                    resp["message"] = "未找到对应生成模型";
                    m_response_json = resp.dump();
                    return DIAGNOSE_REQUEST;
                }

                // 生成指定数量的样本
                std::vector<std::vector<float>> all_results;
                for (int i = 0; i < num_samples; ++i) {
                    // 生成随机噪声 (batch=1, seq_len=64, Z_dim=3)
                    std::vector<float> noise(64 * 3);
                    for (int j = 0; j < 64 * 3; ++j) {
                        noise[j] = ((float)rand() / RAND_MAX) * 2 - 1; // 范围 -1 到 1
                    }
                
                    std::vector<float> result = generator->predict(noise, 64, 3);
                    all_results.push_back(result);
                }

                // 构造 CSV 输出
                std::ostringstream out_csv;
                for (const auto& res : all_results) {
                    for (size_t t = 0; t < 64; ++t) {
                        out_csv << res[t*3] << "," << res[t*3+1] << "," << res[t*3+2] << "\n";
                    }
                }
                m_response_csv = out_csv.str();
                return DIAGNOSE_CSV_REQUEST;
            }
            // 情况2：直接生成（无CSV上传）
            else {
                int num_samples = req.value("num_samples", 1);
                if (num_samples <= 0) num_samples = 1;

                // 直接用默认生成模型（例如 generator_0）生成数据
                ModelInference* generator = generators[0];
                if (!generator) {
                    json resp;
                    resp["status"] = "error";
                    resp["message"] = "默认生成模型未加载";
                    m_response_json = resp.dump();
                    return DIAGNOSE_REQUEST;
                }

                std::vector<std::vector<float>> all_results;
                for (int i = 0; i < num_samples; ++i) {
                    std::vector<float> noise(64 * 3);
                    for (int j = 0; j < 64 * 3; ++j) {
                        noise[j] = ((float)rand() / RAND_MAX) * 2 - 1;
                    }
                    std::vector<float> result = generator->predict(noise, 64, 3);

                    all_results.push_back(result);
                }

                std::ostringstream out_csv;
                for (const auto& res : all_results) {
                    for (size_t t = 0; t < 64; ++t) {
                        out_csv << res[t*3] << "," << res[t*3+1] << "," << res[t*3+2] << "\n";
                    }
                }
                m_response_csv = out_csv.str();
                return DIAGNOSE_CSV_REQUEST;
            }
        } catch (const std::exception& e) {
            json resp;
            resp["status"] = "error";
            resp["message"] = e.what();
            m_response_json = resp.dump();
            return DIAGNOSE_REQUEST;
        }
    }

    // 3. 原有静态文件处理（包括登录注册等）
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/');

    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        char flag = m_url[1];
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3') {
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();
                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            } else
                strcpy(m_url, "/registerError.html");
        } else if (*(p + 1) == '2') {
            if (users.find(name) != users.end() && users[name] == password) {
                std::string token = SessionManager::create_session(name);
                add_status_line(302, "Found");
                add_response("Set-Cookie: session_id=%s; Path=/\r\n", token.c_str());
                add_response("Location: /\r\n");
                add_blank_line();
                return REDIRECT_REQUEST;
            } else {
                strcpy(m_url, "/logError.html");
            }
        }
    }

    // 原有静态文件路由（可保留或删除，视需求而定）
    if (*(p + 1) == '0') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '1') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '5') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '6') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '7') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write() {
    int temp = 0;
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }
    while (1) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp < 0) {
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        if (bytes_to_send <= 0) {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
            if (m_linger) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

bool http_conn::add_response(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    LOG_INFO("request:%s", m_write_buf);
    return true;
}

bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_content_type(const char* content_type) {
    return add_response("Content-Type: %s\r\n", content_type);
}

bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        // 1. 增加 404 处理：如果找不到文件，返回一个简单的错误页面，而不是直接断连
        case NO_RESOURCE:
            add_status_line(404, "Not Found");
            add_headers(strlen("404 Not Found"));
            if (!add_content("404 Not Found - Please check your root path.")) return false;
            break;

        case INTERNAL_ERROR:
            add_status_line(500, "Internal Error");
            add_headers(strlen("Internal Error"));
            if (!add_content("Internal Error")) return false;
            break;

        case FILE_REQUEST:
            add_status_line(200, "OK");
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            } else {
                const char *ok_string = "<html><body>Empty File</body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)) return false;
            }
            break;

        case DIAGNOSE_REQUEST:
            add_status_line(200, "OK");
            add_content_length(m_response_json.size());
            add_content_type("application/json");
            add_linger();
            add_blank_line();
            if (!add_content(m_response_json.c_str())) return false;
            break;

        case DIAGNOSE_CSV_REQUEST:
            add_status_line(200, "OK");
            add_response("Content-Type: text/csv\r\n");
            add_response("Content-Disposition: attachment; filename=\"result.csv\"\r\n");
            add_content_length(m_response_csv.size());
            add_linger();
            add_blank_line();
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = (void*)m_response_csv.data();
            m_iv[1].iov_len = m_response_csv.size();
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_response_csv.size();
            return true;

        default:
            // 兜底：即使出错了也发个 400，不要直接断开
            add_status_line(400, "Bad Request");
            add_headers(0);
            break;
    }

    // 统一设置发送缓冲区（非文件/CSV 的普通响应）
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}