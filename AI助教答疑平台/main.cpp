//  AI 助教答疑平台  ——  个性化注册 / 登录 / 答疑
//  编译(MinGW / w64devkit):  g++ -std=c++17 main.cpp -o ai_platform.exe -lws2_32
//  运行:  ai_platform.exe   然后浏览器打开  http://localhost:8080
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")   

static const int    PORT      = 8080;
static const char*  DATA_FILE = "users.dat";   

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
static std::string to_lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static std::string url_decode(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int h = hexv(s[i + 1]), l = hexv(s[i + 2]);
            if (h >= 0 && l >= 0) { r += (char)((h << 4) | l); i += 2; continue; }
        }
        r += (s[i] == '+') ? ' ' : s[i];
    }
    return r;
}

static std::map<std::string, std::string> parse_form(const std::string& body) {
    std::map<std::string, std::string> m;
    size_t i = 0;
    while (i < body.size()) {
        size_t amp = body.find('&', i);
        std::string kv = (amp == std::string::npos) ? body.substr(i) : body.substr(i, amp - i);
        size_t eq = kv.find('=');
        std::string k = url_decode(eq == std::string::npos ? kv : kv.substr(0, eq));
        std::string v = url_decode(eq == std::string::npos ? "" : kv.substr(eq + 1));
        if (!k.empty() && m.find(k) == m.end()) m[k] = v;   
        if (amp == std::string::npos) break;
        i = amp + 1;
    }
    return m;
}

static std::string hash32(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return buf;
}
static std::string now_token() {
    static uint64_t cnt = 0;
    uint64_t v = (uint64_t)std::time(nullptr) * 1000003ull + (++cnt) + (uint64_t)std::rand();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return buf;
}

struct User {
    std::string username, passhash, nickname, email, role, color, emoji;
};
static std::map<std::string, User>        g_users;
static std::map<std::string, std::string> g_sessions;
static std::vector<std::string>           g_questions;

static void load_users() {
    std::ifstream f(DATA_FILE);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fld;
        std::stringstream ss(line);
        std::string t;
        while (std::getline(ss, t, '\t')) fld.push_back(t);
        if (fld.size() >= 2) {
            User u;
            u.username = fld[0]; u.passhash = fld[1];
            if (fld.size() > 2) u.nickname = fld[2];
            if (fld.size() > 3) u.email    = fld[3];
            if (fld.size() > 4) u.role     = fld[4];
            if (fld.size() > 5) u.color    = fld[5];
            if (fld.size() > 6) u.emoji    = fld[6];
            g_users[u.username] = u;
        }
    }
}
static void save_user(const User& u) {
    std::ofstream f(DATA_FILE, std::ios::app);
    f << u.username << '\t' << u.passhash << '\t' << u.nickname << '\t'
      << u.email << '\t' << u.role << '\t' << u.color << '\t' << u.emoji << '\n';
}


static std::string current_user(const std::string& req) {
    size_t p = 0;
    while (true) {
        size_t e = req.find("\r\n", p);
        std::string line = (e == std::string::npos) ? req.substr(p) : req.substr(p, e - p);
        if (to_lower(line).rfind("cookie:", 0) == 0) {
            std::string val = trim(line.substr(7));
            size_t i = 0;
            while (i < val.size()) {
                size_t sc = val.find(';', i);
                std::string kv = (sc == std::string::npos) ? val.substr(i) : val.substr(i, sc - i);
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = trim(kv.substr(0, eq));
                    std::string v = trim(kv.substr(eq + 1));
                    if (k == "session" && g_sessions.count(v)) return g_sessions[v];
                }
                if (sc == std::string::npos) break;
                i = sc + 1;
            }
        }
        if (e == std::string::npos) break;
        p = e + 2;
        if (p > req.size()) break;
    }
    return "";
}

static std::string PAGE_HEAD(const std::string& title, const std::string& accent = "#2563eb") {
    return
R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>)HTML" + title + R"HTML(</title>
<style>
  :root{ --accent:)HTML" + accent + R"HTML(; }
  *{ box-sizing:border-box; margin:0; padding:0; }
  body{ font-family:"Segoe UI","Microsoft YaHei",sans-serif; background:#f1f3f7;
        min-height:100vh; color:#1f2937; }
  .wrap{ max-width:680px; margin:0 auto; padding:48px 20px; }
  .brand{ text-align:center; margin-bottom:28px; }
  .brand h1{ font-size:30px; letter-spacing:2px; }
  .brand p{ color:#6b7280; margin-top:8px; font-size:14px; }
  .card{ background:#fff; border-radius:14px; box-shadow:0 6px 24px rgba(0,0,0,.06);
         padding:28px; }
  label{ display:block; font-size:14px; color:#374151; margin:14px 0 6px; font-weight:600; }
  input,select,textarea{ width:100%; padding:11px 13px; border:1px solid #d1d5db;
         border-radius:9px; font-size:14px; outline:none; transition:.2s; }
  input:focus,select:focus,textarea:focus{ border-color:var(--accent); box-shadow:0 0 0 3px rgba(37,99,235,.15); }
  .row{ display:flex; gap:12px; } .row>div{ flex:1; }
  .btn{ background:var(--accent); color:#fff; border:none; padding:12px 22px;
        border-radius:9px; font-size:15px; cursor:pointer; margin-top:20px; }
  .btn:hover{ filter:brightness(1.05); }
  .btn.secondary{ background:#eef2ff; color:var(--accent); }
  .hint{ font-size:12px; color:#9ca3af; margin-top:5px; }
  .err{ background:#fef2f2; color:#b91c1c; padding:10px 14px; border-radius:8px;
        font-size:13px; margin-bottom:14px; }
  .topbar{ display:flex; justify-content:space-between; align-items:center; margin-bottom:18px; }
  .topbar a{ color:var(--accent); text-decoration:none; font-size:14px; }
  fieldset{ border:1px solid #e5e7eb; border-radius:10px; padding:12px 16px; margin-top:16px; }
  legend{ padding:0 8px; font-weight:600; color:#374151; }
  .checks{ display:flex; flex-wrap:wrap; gap:10px 18px; margin-top:8px; }
  .checks label{ display:inline-flex; align-items:center; gap:6px; font-weight:400; margin:0; }
  .checks input{ width:auto; }
</style>
</head>
<body>)HTML";
}
static const char* PAGE_TAIL = "\n</body>\n</html>\n";

static std::string page_register(const std::string& err) {
    std::string h = PAGE_HEAD("注册 - AI助教答疑平台");
    h += R"HTML(<div class="wrap">
  <div class="brand"><h1>AI助教答疑平台</h1><p>创建你的个性化账号 · 智能答疑从这里开始</p></div>
  <div class="card">)HTML";
    if (!err.empty()) h += "<div class=\"err\">" + err + "</div>";
    h += R"HTML(
    <form method="post" action="/register" autocomplete="on" novalidate>
      <div class="row">
        <div><label for="username">用户名 *</label>
          <input id="username" name="username" type="text" required minlength="3" maxlength="20"
                 pattern="[A-Za-z0-9_]+" placeholder="3-20位字母/数字/下划线" autocomplete="username"></div>
        <div><label for="nickname">昵称</label>
          <input id="nickname" name="nickname" type="text" maxlength="16" placeholder="想怎么被称呼？" autocomplete="nickname"></div>
      </div>
      <div class="row">
        <div><label for="password">密码 *</label>
          <input id="password" name="password" type="password" required minlength="6" maxlength="32"
                 placeholder="至少6位" autocomplete="new-password"></div>
        <div><label for="confirm">确认密码 *</label>
          <input id="confirm" name="confirm" type="password" required minlength="6"
                 placeholder="再次输入" autocomplete="new-password"></div>
      </div>
      <div class="row">
        <div><label for="email">邮箱 *</label>
          <input id="email" name="email" type="email" required placeholder="you@school.edu.cn" autocomplete="email"></div>
        <div><label for="phone">手机号</label>
          <input id="phone" name="phone" type="tel" pattern="1[3-9]\d{9}" placeholder="选填，11位手机号" autocomplete="tel"></div>
      </div>

      <fieldset>
        <legend>身份与画像（个性化）</legend>
        <label>身份 *</label>
        <div class="checks">
          <label><input type="radio" name="role" value="student" checked required> 学生</label>
          <label><input type="radio" name="role" value="ta"> 助教</label>
          <label><input type="radio" name="role" value="teacher"> 教师</label>
        </div>
        <label for="major">所学专业（带建议列表 datalist）</label>
        <input id="major" name="major" list="majorList" placeholder="输入或选择">
        <datalist id="majorList">
          <option value="计算机科学与技术"><option value="软件工程"><option value="人工智能">
          <option value="数据科学"><option value="网络空间安全"><option value="自动化">
        </datalist>
        <div class="row">
          <div><label for="grade">入学年份</label>
            <input id="grade" name="grade" type="number" min="2015" max="2030" step="1" value="2024"></div>
          <div><label for="birth">出生日期</label>
            <input id="birth" name="birth" type="date"></div>
        </div>
        <label for="site">个人主页</label>
        <input id="site" name="site" type="url" placeholder="https://">
        <div class="row">
          <div><label for="color">主题色</label>
            <input id="color" name="color" type="color" value="#2563eb"></div>
          <div><label for="level">自评熟练度 <output id="lv" for="level">5</output></label>
            <input id="level" name="level" type="range" min="1" max="10" value="5"
                   oninput="document.getElementById('lv').value=this.value"></div>
        </div>
        <label>感兴趣的课程（可多选）</label>
        <div class="checks">
          <label><input type="checkbox" name="course" value="数据结构"> 数据结构</label>
          <label><input type="checkbox" name="course" value="操作系统"> 操作系统</label>
          <label><input type="checkbox" name="course" value="计算机网络"> 计算机网络</label>
          <label><input type="checkbox" name="course" value="机器学习"> 机器学习</label>
          <label><input type="checkbox" name="course" value="数据库"> 数据库</label>
        </div>
      </fieldset>

      <label for="intro">自我介绍</label>
      <textarea id="intro" name="intro" rows="3" maxlength="200" placeholder="一句话介绍自己..."></textarea>

      <div class="checks" style="margin-top:16px">
        <label><input type="checkbox" name="notice" checked> 接收答疑通知邮件</label>
        <label><input type="checkbox" name="agree" required> 我已阅读并同意《用户协议》*</label>
      </div>
      <div class="row">
        <button class="btn" type="submit">注 册</button>
        <button class="btn secondary" type="reset">重 置</button>
      </div>
      <p style="text-align:center;margin-top:16px;font-size:14px;color:#6b7280">
        已有账号？<a href="/login" style="color:var(--accent)">去登录</a></p>
    </form>
  </div>
</div>)HTML";
    h += PAGE_TAIL;
    return h;
}

static std::string page_login(const std::string& err) {
    std::string h = PAGE_HEAD("登录 - AI助教答疑平台");
    h += R"HTML(<div class="wrap">
  <div class="brand"><h1>AI助教答疑平台</h1><p>智能答疑 · 即时提问</p></div>
  <div class="card">)HTML";
    if (!err.empty()) h += "<div class=\"err\">" + err + "</div>";
    h += R"HTML(
    <form method="post" action="/login" autocomplete="on">
      <label for="username">用户名</label>
      <input id="username" name="username" type="text" required autocomplete="username" placeholder="请输入用户名">
      <label for="password">密码</label>
      <input id="password" name="password" type="password" required autocomplete="current-password" placeholder="请输入密码">
      <div class="checks" style="margin-top:14px">
        <label><input type="checkbox" name="remember" checked> 记住我</label>
        <label><input type="checkbox" onclick="var p=document.getElementById('password');p.type=this.checked?'text':'password'"> 显示密码</label>
      </div>
      <button class="btn" type="submit" style="width:100%">登 录</button>
      <p style="text-align:center;margin-top:16px;font-size:14px;color:#6b7280">
        还没有账号？<a href="/register" style="color:var(--accent)">立即注册</a></p>
    </form>
  </div>
</div>)HTML";
    h += PAGE_TAIL;
    return h;
}

static std::string page_home(const std::string& user) {
    std::string accent = "#2563eb", name = user, role = "同学";
    if (!user.empty() && g_users.count(user)) {
        const User& u = g_users[user];
        if (!u.color.empty()) accent = u.color;
        if (!u.nickname.empty()) name = u.nickname;
        if (!u.role.empty()) role = (u.role == "teacher" ? "老师" : (u.role == "ta" ? "助教" : "同学"));
    }
    std::string h = PAGE_HEAD("AI助教答疑平台", accent);
    h += "<div class=\"wrap\">";
    h += "<div class=\"brand\"><h1>AI助教答疑平台</h1><p>智能答疑 · 即时提问</p></div>";
    h += "<div class=\"card\">";
    h += "<div class=\"topbar\">";
    if (user.empty())
        h += "<span style=\"color:#6b7280;font-size:14px\">游客模式 · 提问需先登录</span>"
             "<span><a href=\"/login\">登录</a> &nbsp; <a href=\"/register\">注册</a></span>";
    else
        h += "<span style=\"font-size:14px\">你好，<b>" + name + "</b>（" + role + "）</span>"
             "<a href=\"/logout\">退出登录</a>";
    h += "</div>";

    h += R"HTML(<form method="post" action="/ask">
      <div class="row">
        <input name="q" type="text" placeholder="输入你的问题，例如：什么是时间复杂度？" required maxlength="200">
        <button class="btn" type="submit" style="margin-top:0;white-space:nowrap">提问</button>
      </div></form>)HTML";

    if (g_questions.empty()) {
        h += "<p style=\"text-align:center;color:#9ca3af;margin-top:26px;font-size:14px\">还没有问题，快来提出第一个吧～</p>";
    } else {
        for (size_t i = 0; i < g_questions.size(); i++) {
            int n = (int)i + 1;
            std::string body;
            if (n < 9)
                body = "这是第 " + std::to_string(n) + " 个问题，AI接口将在第9轮接入。";
            else
                body = "（AI已接入）针对“" + g_questions[i] + "”的模拟解答：这是一个很好的问题，建议从基础概念入手逐步分析。";
            h += "<div style=\"margin-top:16px;padding:16px;border:1px solid #eef2ff;"
                 "border-left:4px solid var(--accent);border-radius:10px;background:#fafcff\">"
                 "<div style=\"font-weight:600;color:#111827\">Q" + std::to_string(n) + "：" + g_questions[i] + "</div>"
                 "<div style=\"margin-top:8px;color:#4b5563;font-size:14px\">" + body + "</div></div>";
        }
    }
    h += "</div></div>";
    h += PAGE_TAIL;
    return h;
}

struct Response {
    int code = 200;
    std::string type = "text/html; charset=utf-8";
    std::string body;
    std::string extra;   
};

static Response handle(const std::string& method, const std::string& path,
                       const std::string& body, const std::string& req) {
    Response r;
    auto redirect = [](const std::string& loc, const std::string& cookie = "") {
        Response x; x.code = 302; x.extra = "Location: " + loc + "\r\n";
        if (!cookie.empty()) x.extra += "Set-Cookie: " + cookie + "\r\n";
        return x;
    };

    if (method == "GET" && path == "/") {
        r.body = page_home(current_user(req));
        return r;
    }
    if (method == "GET" && path == "/register") {
        r.body = page_register("");
        return r;
    }
    if (method == "GET" && path == "/login") {
        r.body = page_login("");
        return r;
    }
    if (method == "GET" && path == "/logout") {
        std::string u = current_user(req);
        for (auto it = g_sessions.begin(); it != g_sessions.end(); )
            it = (it->second == u) ? g_sessions.erase(it) : std::next(it);
        return redirect("/", "session=; Path=/; Max-Age=0");
    }
    if (method == "POST" && path == "/register") {
        auto f = parse_form(body);
        std::string username = f["username"], pw = f["password"], cf = f["confirm"];
        if (username.empty() || pw.empty()) { r.body = page_register("用户名和密码不能为空"); return r; }
        if (pw != cf) { r.body = page_register("两次输入的密码不一致"); return r; }
        if (g_users.count(username)) { r.body = page_register("该用户名已被注册"); return r; }
        if (!f.count("agree")) { r.body = page_register("请先同意《用户协议》"); return r; }
        User u;
        u.username = username;
        u.passhash = hash32(pw);
        u.nickname = f.count("nickname") ? f["nickname"] : username;
        u.email    = f["email"];
        u.role     = f.count("role") ? f["role"] : "student";
        u.color    = f.count("color") ? f["color"] : "#2563eb";
        u.emoji    = "hi";
        g_users[username] = u;
        save_user(u);
        std::string tok = now_token();
        g_sessions[tok] = username;
        return redirect("/", "session=" + tok + "; Path=/; HttpOnly");
    }
    if (method == "POST" && path == "/login") {
        auto f = parse_form(body);
        std::string username = f["username"], pw = f["password"];
        if (!g_users.count(username) || g_users[username].passhash != hash32(pw)) {
            r.body = page_login("用户名或密码错误");
            return r;
        }
        std::string tok = now_token();
        g_sessions[tok] = username;
        std::string age = f.count("remember") ? "; Max-Age=604800" : "";
        return redirect("/", "session=" + tok + "; Path=/; HttpOnly" + age);
    }
    if (method == "POST" && path == "/ask") {
        auto f = parse_form(body);
        std::string q = trim(f["q"]);
        if (!q.empty()) g_questions.push_back(q);
        return redirect("/");
    }
    r.code = 404;
    r.body = "<div style='text-align:center;padding:80px;font-family:sans-serif'>"
             "<h2>404</h2><p>页面不存在 <a href='/'>返回首页</a></p></div>";
    return r;
}

static std::string read_request(SOCKET s) {
    std::string req;
    char buf[8192];
    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return req;
        req.append(buf, n);
        header_end = req.find("\r\n\r\n");
    }
    long long len = 0;
    {
        std::string h = req.substr(0, header_end);
        size_t p = 0;
        while (true) {
            size_t e = h.find("\r\n", p);
            std::string line = (e == std::string::npos) ? h.substr(p) : h.substr(p, e - p);
            if (to_lower(line).rfind("content-length:", 0) == 0)
                len = std::atoll(trim(line.substr(15)).c_str());
            if (e == std::string::npos) break;
            p = e + 2;
        }
    }
    while ((long long)(req.size() - (header_end + 4)) < len) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, n);
    }
    return req;
}

static void send_all(SOCKET s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = send(s, data.data() + sent, (int)(data.size() - sent), 0);
        if (n <= 0) break;
        sent += n;
    }
}

int main() {
    std::srand((unsigned)std::time(nullptr));
    load_users();

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::printf("WSAStartup 失败\n");
        return 1;
    }
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) { std::printf("socket 失败\n"); return 1; }
    int yes = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::printf("端口 %d 绑定失败，可能已被占用\n", PORT);
        return 1;
    }
    listen(listen_sock, 16);
    std::printf("=========================================\n");
    std::printf(" AI助教答疑平台已启动\n");
    std::printf(" 请在浏览器打开: http://localhost:%d\n", PORT);
    std::printf(" 按 Ctrl+C 停止服务\n");
    std::printf("=========================================\n");

    while (true) {
        SOCKET c = accept(listen_sock, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        std::string req = read_request(c);
        if (req.empty()) { closesocket(c); continue; }

        std::string method, path;
        {
            size_t sp1 = req.find(' ');
            size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : req.find(' ', sp1 + 1);
            if (sp1 != std::string::npos && sp2 != std::string::npos) {
                method = req.substr(0, sp1);
                path   = req.substr(sp1 + 1, sp2 - sp1 - 1);
            }
        }
        size_t q = path.find('?');
        std::string route = (q == std::string::npos) ? path : path.substr(0, q);
        size_t he = req.find("\r\n\r\n");
        std::string body = (he == std::string::npos) ? "" : req.substr(he + 4);

        Response res = handle(method, route, body, req);

        std::string head = "HTTP/1.1 " + std::to_string(res.code) +
            (res.code == 200 ? " OK" : (res.code == 302 ? " Found" : " Not Found")) + "\r\n";
        head += "Content-Type: " + res.type + "\r\n";
        head += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
        head += "Connection: close\r\n";
        head += res.extra;
        head += "\r\n";
        send_all(c, head + res.body);
        closesocket(c);
    }
    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
