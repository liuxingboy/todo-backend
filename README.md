# 🚀 GTD-Master: 基于 C++20 协程的高性能效能管理与共享云盘系统

![C++](https://img.shields.io/badge/C++-20-blue.svg)
![Drogon](https://img.shields.io/badge/Framework-Drogon-brightgreen.svg)
![MySQL](https://img.shields.io/badge/Database-MySQL-blue.svg)
![Flutter](https://img.shields.io/badge/Frontend-Flutter-45D1FD.svg)

## 📖 项目简介
GTD-Master 是一个全栈落地的高性能个人效能管理（Getting Things Done）与团队文件共享系统。
后端采用现代化 **C++20** 编写，基于当前霸榜 TechEmpower 的 C++ Web 框架 **Drogon** 构建，全面拥抱无栈协程（Coroutines）以实现极致的高并发处理能力。前端采用 **Flutter** 跨平台开发，提供丝滑的现代 Material 3 响应式 UI。

本项目不仅实现了闭环的 GTD 业务流，更集成了带有严格权限校验的微型云盘功能，具备企业级的数据隔离与安全设计。

---

## 🛠️ 技术栈 (Tech Stack)

### 后端 (Backend)
* **核心语言**: C++ 20
* **Web 框架**: Drogon (基于 epoll/kqueue 的高性能异步网络框架)
* **数据库**: MySQL 8.0 (Docker 容器化部署)
* **鉴权机制**: JWT (JSON Web Token) 双令牌无感刷新方案
* **核心特性**: C++20 协程 (`co_await` / `co_return`)、RESTful API 设计、跨域 (CORS) 拦截器、多租户数据隔离。

### 前端 (Frontend)
* **框架**: Flutter (Dart)
* **网络请求**: Dio (集成全局 Token 拦截器与 401 无感刷新)
* **状态管理**: Provider
* **本地存储**: SharedPreferences

---

## ✨ 核心特性与架构亮点 (Key Features & Highlights)

### 1. 🛡️ 工业级鉴权与多租户隔离
* **双 Token 机制**: 实现短期 `access_token` (鉴权) 与长期 `refresh_token` (保活) 的分离，前端实现 401 拦截无感刷新，兼顾安全性与极佳的用户体验。
* **严格的防越权设计**: 所有资源操作 (CRUD) 底层 SQL 均与 `user_id` 强绑定，杜绝水平越权 (IDOR) 漏洞。

### 2. ✅ GTD 核心引擎 (状态流转)
* **任务池引擎**: 支持创建分类任务池 (Task Pools) 与模板任务，支持级联删除 (Cascade Delete)，清空池子自动销毁孤儿数据。
* **“我的一天”流转**: 通过核心 SQL JOIN 查询，将任务从静态池中“实例化”至当日待办，支持动态打卡与时间戳记录。
* **DBA 级自动清理**: 利用 MySQL 原生事件调度器 (Event Scheduler)，每 30 天自动清理一年前已完成的历史任务，防止单表数据膨胀。

### 3. ☁️ 高性能共享网盘 (Binary I/O)
* **大文件吞吐**: 突破框架默认限制，支持高达 **1GB** 大小的二进制文件上传 (Multipart Form Data)，底采用异步流式写入降低内存占用。
* **文件流下发**: 支持 `application/octet-stream` 高效文件流下载。
* **细粒度权限**: 共享大厅所有用户可见，但物理文件与数据库记录的“删除权”严格限制为**文件上传者本人**。

### 4. 📝 灵感便签模块
* 提供极简的富文本/纯文本随手记功能，按更新时间戳动态倒序排列。

---

## 🗄️ 数据库设计 (Database Schema)
系统包含 6 张经过严格第三范式 (3NF) 设计的核心表：
1. `users`: 用户凭证与长令牌存储
2. `task_pools`: 任务池分类目录
3. `pool_tasks`: 静态任务模板池
4. `my_day_tasks`: 实例化的每日任务清单
5. `notes`: 随手记便签
6. `shared_files`: 二进制文件资源映射表

---

## 🚀 本地编译与运行 (How to run)

### 1. 环境依赖
* Linux (CentOS/Ubuntu) 或 macOS
* GCC 11+ / Clang 14+ (必须支持 C++20)
* CMake 3.10+
* 预装 Drogon 环境及 Jsoncpp, uuid 等依赖

### 2. 启动数据库
```bash
# 启动 MySQL 容器并挂载数据卷
docker run --name todo_mysql -e MYSQL_ROOT_PASSWORD=your_password -e MYSQL_DATABASE=todo_db -p 3306:3306 -d mysql:8.0
