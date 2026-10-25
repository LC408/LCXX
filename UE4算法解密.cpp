// Split from draw implementation. Included by 绘制.cpp to keep the original translation unit.

#ifdef YIBAO_DRAW_INLINE_RESOURCES

// 修复函数声明语法
std::vector<HookRW::MemSeg> 获取内存段(pid_t pid)
{
    std::vector<HookRW::MemSeg> segments;
    char path[128];
    snprintf(path, sizeof(path), AY_OBFUSCATE("/proc/%d/maps"), pid);

    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        // 文件打开失败，静默处理
        return segments;
    }

    char *line = nullptr;
    size_t length = 0;
    while (getline(&line, &length, fp) > 0)
    {
        uintptr_t start, end;
        char flags[8] = {0};
        if (sscanf(line, "%lx-%lx %7s", &start, &end, flags) == 3)
        {
            if (strstr(flags, "rw-p"))
            {
                segments.push_back({start, end});
            }
        }
    }

    free(line);
    fclose(fp);
    return segments;
}

// ========== 算法解密（无漏拾荒引擎）相关全局变量 ==========
std::vector<uintptr_t> 全局_自建Actor池;
std::unordered_map<uintptr_t, std::chrono::steady_clock::time_point> 全局_自建Actor缓存;
std::unordered_map<uintptr_t, bool> 全局_自建Actor人物标记;
std::mutex Actor池_线程锁;
std::atomic<bool> 拾荒引擎运行中{false};
std::chrono::steady_clock::time_point 全局_自建Actor池更新时间{};
// 真人=残留 同特征：禁止一切「猜退出」。只认死/HP。残留靠较长 TTL 慢清。
static constexpr int 算法人物候选有效秒 = 4;
static constexpr int 算法人物确认有效秒 = 15;
static constexpr int 算法人物缓存有效秒 = 算法人物候选有效秒;
static constexpr int 算法物资缓存有效秒 = 6;

static inline bool 是有效用户态地址(uintptr_t addr) {
    return addr > 0x1000000000ull && addr < 0x8000000000ull;
}

// 9/9 真实 ULevel 离册快照一致：生命周期位清零，两个归属链指针同时清零。
// 名字/血/479.5/Mesh 仍保持，正是退出残留被扫描重新收回的原因。
static constexpr uintptr_t 算法退出生命周期偏移 = 0x38;
static constexpr uintptr_t 算法退出归属链A偏移 = 0x1170;
static constexpr uintptr_t 算法退出归属链B偏移 = 0x1B50;
static constexpr uint64_t 算法退出生命周期值 = 0x0000000E00000000ull;

static inline bool 是算法退出残留签名(uint64_t lifecycle,
                                      uintptr_t ownerA,
                                      uintptr_t ownerB) {
    return lifecycle == 算法退出生命周期值 && ownerA == 0 && ownerB == 0;
}

static uintptr_t 获取libUE4结束地址(pid_t pid, uintptr_t libBase) {
    static pid_t s_pid = 0;
    static uintptr_t s_base = 0;
    static uintptr_t s_end = 0;
    if (pid == s_pid && libBase == s_base && s_end > libBase) {
        return s_end;
    }

    uintptr_t endAddr = 0;
    char path[128];
    snprintf(path, sizeof(path), AY_OBFUSCATE("/proc/%d/maps"), pid);
    FILE *fp = fopen(path, "r");
    if (fp) {
        char *line = nullptr;
        size_t length = 0;
        while (getline(&line, &length, fp) > 0) {
            if (strstr(line, "libUE4.so") == nullptr) continue;
            uintptr_t start = 0, end = 0;
            if (sscanf(line, "%lx-%lx", &start, &end) == 2 && end > endAddr) {
                endAddr = end;
            }
        }
        free(line);
        fclose(fp);
    }

    if (endAddr <= libBase) {
        endAddr = libBase + 0x42000000ull;
    }
    s_pid = pid;
    s_base = libBase;
    s_end = endAddr;
    return s_end;
}

static inline bool 是libUE4模块地址(uintptr_t addr, uintptr_t libBase, pid_t pid) {
    if (!是有效用户态地址(addr) || !是有效用户态地址(libBase)) return false;
    return addr >= libBase && addr < 获取libUE4结束地址(pid, libBase);
}

static inline bool 是问号字节(unsigned char c) {
    return c == '?' || c == 0xEF; // 半角 ?；全角 ？ UTF-8 以 EF 起，下面整串再判
}

static inline bool 是空气人名字(const std::string& name) {
    if (name.empty()) return true;
    if (name == "-1" || name == "NULL" || name == "null" || name == "nil") return true;
    if (name == "敌" || name == "Enemy" || name == "enemy" || name == "Player") return true;
    // 绘制侧占位名；假点长期以 ? / ?? / 全角？ 出现
    if (name == "?" || name == "??" || name == "???" || name == "????") return true;
    if (name == u8"？" || name == u8"？？" || name == u8"？？？" || name == u8"？？？？") return true;
    // 全是半角/全角问号也当空气名
    bool all_q = true;
    for (size_t i = 0; i < name.size();) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (c == '?') {
            ++i;
            continue;
        }
        // UTF-8 全角问号 U+FF1F = EF BC 9F
        if (i + 2 < name.size() &&
            static_cast<unsigned char>(name[i]) == 0xEF &&
            static_cast<unsigned char>(name[i + 1]) == 0xBC &&
            static_cast<unsigned char>(name[i + 2]) == 0x9F) {
            i += 3;
            continue;
        }
        all_q = false;
        break;
    }
    if (all_q) return true;
    return false;
}

// 扫到的 479.5 假点常挂着堆上随机 FString；只排除 ? 不够
// 要求：长度合理 + 可打印/中文为主 + 至少 2 个有效字符
static inline bool 是有效玩家名(const std::string& name) {
    if (是空气人名字(name)) return false;
    if (name.size() < 2 || name.size() > 48) return false;
    int good = 0;
    int bad = 0;
    for (size_t i = 0; i < name.size();) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20 || c == 0x7F) {
            ++bad;
            ++i;
            continue;
        }
        if (c < 0x80) {
            // ASCII：字母数字下划线空格常见；其它符号少许允许
            if ((c >= '0' && c <= '9') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                c == '_' || c == '-' || c == ' ') {
                ++good;
            } else if (c == '.' || c == '[' || c == ']' || c == '(' || c == ')') {
                // 允许但不计 good
            } else {
                ++bad;
            }
            ++i;
            continue;
        }
        // UTF-8 多字节：接受合法前导，计为 good（中文名）
        int need = 0;
        if ((c & 0xE0) == 0xC0) need = 1;
        else if ((c & 0xF0) == 0xE0) need = 2;
        else if ((c & 0xF8) == 0xF0) need = 3;
        else {
            ++bad;
            ++i;
            continue;
        }
        if (i + need >= name.size()) {
            ++bad;
            break;
        }
        bool ok = true;
        for (int k = 1; k <= need; ++k) {
            unsigned char cc = static_cast<unsigned char>(name[i + k]);
            if ((cc & 0xC0) != 0x80) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            ++bad;
            ++i;
            continue;
        }
        ++good;
        i += static_cast<size_t>(need) + 1;
    }
    if (good < 2) return false;
    if (bad > good) return false;
    return true;
}

// 真人血量窗：放宽一点，防读抖/护甲加成被当假点
static inline bool 是合理人物血(float hp, float hpmax, bool 允许空血) {
    if (!std::isfinite(hp) || !std::isfinite(hpmax)) return false;
    if (hpmax < 5.0f || hpmax > 500.0f) return false;
    if (hp < -0.5f || hp > hpmax + 120.0f) return false;
    if (!允许空血 && hp <= 0.0f) return false;
    return true;
}

// 本局己方队伍（死亡/观战期间 自身地址 会切到队友 pawn，+0xb78 可能读偏；
// 存活时持续缓存，观战时用缓存 + 每帧同队 actor 集合过滤队友）
static int g_本局己方队伍B78 = -1;
static int g_本局己方队伍B68 = -1;
static int g_队伍缓存_pid = 0;

static inline bool 队伍编号有效(int tid) {
    return tid > 0 && tid <= 200;
}

static inline bool Actor与己方同队(uintptr_t actor, int tid_b78, int tid_b68, int 当前自身队伍) {
    if (actor == 0) return false;
    if (tid_b78 < 0 || tid_b78 > 1000) return true;
    if (g_本局己方队伍B78 > 0 && (tid_b78 == g_本局己方队伍B78 || tid_b68 == g_本局己方队伍B78)) return true;
    if (g_本局己方队伍B68 > 0 && (tid_b78 == g_本局己方队伍B68 || tid_b68 == g_本局己方队伍B68)) return true;
    if (队伍编号有效(当前自身队伍) && tid_b78 == 当前自身队伍) return true;
    return false;
}

static bool 是算法缓存人物(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(Actor池_线程锁);
    auto it = 全局_自建Actor人物标记.find(actor);
    return it != 全局_自建Actor人物标记.end() && it->second;
}

static int 获取算法缓存年龄毫秒(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(Actor池_线程锁);
    auto it = 全局_自建Actor缓存.find(actor);
    if (it == 全局_自建Actor缓存.end()) return 2147483647;
    auto age = std::chrono::steady_clock::now() - it->second;
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(age).count();
}

// 失败计数：已确认扛约 0.5s；候选 2 次就走
static std::unordered_map<uintptr_t, int> 解密失败计数;
static std::mutex 失败计数锁;
static constexpr int 容忍失败阈值 = 30;
static constexpr int 候选失败阈值 = 4;

static void 重置解密失败计数(uintptr_t actor) {
    if (!是有效用户态地址(actor)) return;
    std::lock_guard<std::mutex> lock(失败计数锁);
    解密失败计数.erase(actor);
}

// ========== 算法人物状态机 ==========
// 设计目标：
// 1) 进池/确认：真名 + 有效队 + 479.5 + Mesh
// 2) 续命：真名+合理血，且坐标/速度有活动（退局残留会冻住）
// 3) 死/退硬踢；坐标长时间纹丝不动 → 当退出拉黑
struct 算法人物状态 {
    int conf = 0;
    bool confirmed = false;
    std::chrono::steady_clock::time_point last_ok{};
    std::chrono::steady_clock::time_point last_seen{};
    D3DVector last_pos{};
    bool has_pos = false;
    D3DVector activity_pos{};   // 最近一次「有活动」的坐标
    bool has_activity_pos = false;
    int stagnant_frames = 0;    // 连续坐标几乎不动
    int miss_world_frames = 0;  // 连续不在世界数组（退出辅助信号）
    bool saw_world = false;     // 曾经出现在 ULevel 世界数组
    bool saw_controller = false; // 曾经读到有效 Controller
    int ctrl_dead_frames = 0;    // Controller 持续失效帧
    bool saw_mesh = false;
    int mesh_dead_frames = 0;
    std::string sticky_name;
    std::chrono::steady_clock::time_point sticky_name_time{};
    int no_name_frames = 0;
    int no_live_frames = 0;
    int soft_fail_frames = 0;
};
static std::unordered_map<uintptr_t, 算法人物状态> 算法人物表;
static std::unordered_map<uintptr_t, std::chrono::steady_clock::time_point> 算法人物黑名单;
// TTL 出池后再进禁：打断「掉了又被扫描捞回」的空气人循环；比死亡黑名单稍长
static std::unordered_map<uintptr_t, std::chrono::steady_clock::time_point> 算法TTL禁入表;
static std::mutex 确认帧锁;
static constexpr int 算法人物确认阈值 = 2; // 降确认成本，防进池慢
static constexpr auto 算法确认断帧间隔 = std::chrono::milliseconds(900);
static constexpr auto 算法粘名有效 = std::chrono::seconds(15);
static constexpr auto 算法确认表过期 = std::chrono::seconds(25);
static constexpr auto 算法黑名单时长 = std::chrono::seconds(6); // 仅明确死亡/HP<=0
static constexpr auto 算法TTL禁入时长 = std::chrono::seconds(12); // 出池后 12s 扫描不收同地址
static constexpr int 算法候选无真名踢帧 = 24;
static constexpr int 算法确认无真名踢帧 = 180;   // ~3s 名抖且无粘名
// 内存名持续空气（？？/空）：粘名只护显示，不护存活。退局残留常挂 479.5+旧粘名
static constexpr int 算法确认内存空气名踢帧 = 90; // ~1.5s 本帧真名没了就硬清
static constexpr int 算法确认无活信号踢帧 = 180;
static constexpr int 算法确认软失败可画帧 = 180; // ~3s 479.5/Mesh 抖仍画，防闪
// 只开「曾在世界数组 → 持续不在」：退局 pawn 常仍 479.5/名/血，但不在 ULevel。
// 从未进过世界数组的（纯扫描进池）不判，避免真人被误踢。
// bit冻/Controller 仍关：站桩真人同特征。
static constexpr uintptr_t 算法人物Controller偏移 = 0x5f58;
static constexpr int 算法Controller失联踢帧 = 1000000;
static constexpr int 算法Controller停续命帧 = 1000000;
static constexpr int 算法Mesh失联辅助帧 = 1000000;
static constexpr int 算法世界缺席辅助帧 = 1000000;
static constexpr int 算法残留bit冻踢帧 = 1000000;
static constexpr int 算法残留bit冻停续命帧 = 1000000;
static constexpr int 算法残留双条件冻帧 = 1000000;
static constexpr int 算法坐标冻结踢帧 = 1000000;
static constexpr float 算法活动位移cm = 0.01f;
static constexpr float 算法活动速度阈值 = 1.0f;
static constexpr int 算法冻结停续命帧 = 1000000;
// 曾在册后连续缺席：~1.5s 停续命，~2.5s 硬清+拉黑
static constexpr int 算法世界缺席停续命帧 = 90;
static constexpr int 算法世界缺席快踢帧 = 150;
static constexpr int 算法缺席叠冻结快踢 = 1000000;

static void 加入算法人物黑名单(uintptr_t actor) {
    if (!是有效用户态地址(actor)) return;
    std::lock_guard<std::mutex> lock(确认帧锁);
    算法人物黑名单[actor] = std::chrono::steady_clock::now();
}

static bool 在算法人物黑名单(uintptr_t actor) {
    if (!是有效用户态地址(actor)) return false;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物黑名单.find(actor);
    if (it == 算法人物黑名单.end()) return false;
    if (now - it->second > 算法黑名单时长) {
        算法人物黑名单.erase(it);
        return false;
    }
    return true;
}

// 扫描/冷启动用：死亡黑名单 或 TTL 出池禁入
static bool 算法人物禁止新进(uintptr_t actor) {
    if (!是有效用户态地址(actor)) return true;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto itb = 算法人物黑名单.find(actor);
    if (itb != 算法人物黑名单.end()) {
        if (now - itb->second > 算法黑名单时长) 算法人物黑名单.erase(itb);
        else return true;
    }
    auto itt = 算法TTL禁入表.find(actor);
    if (itt != 算法TTL禁入表.end()) {
        if (now - itt->second > 算法TTL禁入时长) 算法TTL禁入表.erase(itt);
        else return true;
    }
    return false;
}

static void 加入算法TTL禁入(uintptr_t actor) {
    if (!是有效用户态地址(actor)) return;
    std::lock_guard<std::mutex> lock(确认帧锁);
    算法TTL禁入表[actor] = std::chrono::steady_clock::now();
}

static void 清除算法人物确认(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    算法人物表.erase(actor);
}

static void 清理过期算法人物确认() {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(确认帧锁);
    for (auto it = 算法人物表.begin(); it != 算法人物表.end();) {
        if (now - it->second.last_seen > 算法确认表过期) {
            it = 算法人物表.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = 算法人物黑名单.begin(); it != 算法人物黑名单.end();) {
        if (now - it->second > 算法黑名单时长) it = 算法人物黑名单.erase(it);
        else ++it;
    }
    for (auto it = 算法TTL禁入表.begin(); it != 算法TTL禁入表.end();) {
        if (now - it->second > 算法TTL禁入时长) it = 算法TTL禁入表.erase(it);
        else ++it;
    }
}

static bool 算法人物已确认(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    return it != 算法人物表.end() && it->second.confirmed;
}

static std::string 获取算法粘名(uintptr_t actor) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    if (it == 算法人物表.end()) return {};
    if (it->second.sticky_name.empty()) return {};
    if (now - it->second.sticky_name_time > 算法粘名有效) return {};
    return it->second.sticky_name;
}

static void 设置算法粘名(uintptr_t actor, const std::string& name) {
    if (!是有效玩家名(name)) return;
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto& st = 算法人物表[actor];
    st.sticky_name = name;
    st.sticky_name_time = std::chrono::steady_clock::now();
    st.no_name_frames = 0;
}

// 好帧推进确认；返回当前 conf。断帧只衰减不直接清零（防真人漏）
static int 推进算法人物确认(uintptr_t actor, bool good_frame) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto& st = 算法人物表[actor];
    st.last_seen = now;
    if (!good_frame) {
        // 已确认：坏帧不降 conf；候选：坏帧 conf 减半衰减
        if (!st.confirmed && st.conf > 0) st.conf = st.conf > 1 ? st.conf - 1 : 0;
        return st.conf;
    }
    if (st.last_ok.time_since_epoch().count() != 0 &&
        (now - st.last_ok) > 算法确认断帧间隔) {
        // 候选断太久：从 1 重新攒；已确认保持
        if (!st.confirmed) st.conf = 0;
    }
    st.last_ok = now;
    if (st.conf < 1000) ++st.conf;
    if (st.conf >= 算法人物确认阈值) st.confirmed = true;
    return st.conf;
}

static int 获取算法人物确认(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    return it == 算法人物表.end() ? 0 : it->second.conf;
}

static void 重置算法人物确认(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    if (it == 算法人物表.end()) return;
    // 已确认只降 conf，不丢 sticky/confirmed，避免真人一抖就整段消失
    if (it->second.confirmed) {
        it->second.conf = 算法人物确认阈值; // 保持可画
        return;
    }
    it->second.conf = 0;
    it->second.has_pos = false;
}

static int 算法人物无真名计数(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto& st = 算法人物表[actor];
    // 不刷新 last_seen：无真名不应阻止状态表过期
    return ++st.no_name_frames;
}

static void 算法人物无真名清零(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    if (it != 算法人物表.end()) it->second.no_name_frames = 0;
}

static int 算法人物无活信号计数(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto& st = 算法人物表[actor];
    return ++st.no_live_frames;
}

static void 算法人物活信号清零(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    if (it != 算法人物表.end()) it->second.no_live_frames = 0;
}

static int 算法人物软失败计数(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto& st = 算法人物表[actor];
    return ++st.soft_fail_frames;
}

static void 算法人物软失败清零(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    if (it != 算法人物表.end()) it->second.soft_fail_frames = 0;
}

static int 算法人物取软失败帧(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    return it == 算法人物表.end() ? 0 : it->second.soft_fail_frames;
}

static bool 算法人物取上次坐标(uintptr_t actor, D3DVector& out) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    if (it == 算法人物表.end() || !it->second.has_pos) return false;
    out = it->second.last_pos;
    return true;
}

// 返回 true = 本帧坐标跳变过大
static bool 算法人物坐标跳变(uintptr_t actor, const D3DVector& nowPos, float maxJumpCm) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto& st = 算法人物表[actor];
    if (!st.has_pos) {
        st.last_pos = nowPos;
        st.has_pos = true;
        st.activity_pos = nowPos;
        st.has_activity_pos = true;
        st.stagnant_frames = 0;
        return false;
    }
    float dx = nowPos.X - st.last_pos.X;
    float dy = nowPos.Y - st.last_pos.Y;
    float dz = nowPos.Z - st.last_pos.Z;
    float d2 = dx * dx + dy * dy + dz * dz;
    st.last_pos = nowPos;
    return d2 > maxJumpCm * maxJumpCm;
}

// 记活动：只看坐标 bit。退局残留坐标完全冻死；站桩有浮点微抖。
// 速度不参与：退出 pawn 可能残留非 0 速度，用速度会永远清不掉。
static int 算法人物记活动(uintptr_t actor, const D3DVector& nowPos, float /*speedCm*/) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto& st = 算法人物表[actor];
    if (!st.has_activity_pos) {
        st.activity_pos = nowPos;
        st.has_activity_pos = true;
        st.stagnant_frames = 0;
        return 0;
    }
    if (memcmp(&nowPos, &st.activity_pos, sizeof(D3DVector)) != 0) {
        st.activity_pos = nowPos;
        st.stagnant_frames = 0;
    } else {
        if (st.stagnant_frames < 100000) ++st.stagnant_frames;
    }
    return st.stagnant_frames;
}

static int 算法人物取冻结帧(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    return it == 算法人物表.end() ? 0 : it->second.stagnant_frames;
}

static int 算法人物取世界缺席帧(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    return it == 算法人物表.end() ? 0 : it->second.miss_world_frames;
}

// 世界数组：仅「曾经在册」后才累计缺席。纯扫描进池、从未在 ULevel 的不判。
// list_valid=false（本帧数组不可用）时不累计、不清零，防数组抖空误杀。
static int 算法人物记世界数组(uintptr_t actor, bool in_world, bool list_valid) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto& st = 算法人物表[actor];
    if (!list_valid) {
        return st.saw_world ? st.miss_world_frames : 0;
    }
    if (in_world) {
        st.saw_world = true;
        st.miss_world_frames = 0;
        return 0;
    }
    if (!st.saw_world) return 0;
    if (st.miss_world_frames < 100000) ++st.miss_world_frames;
    return st.miss_world_frames;
}

static bool 算法人物曾在世界数组(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    return it != 算法人物表.end() && it->second.saw_world;
}

// Controller 跟踪：只认「曾经有效 → 持续失效」= 退出；从未有过 Controller 的（部分人机）不判
static int 算法人物记Controller(uintptr_t actor, bool ctrl_ok) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto& st = 算法人物表[actor];
    if (ctrl_ok) {
        st.saw_controller = true;
        st.ctrl_dead_frames = 0;
    } else if (st.saw_controller) {
        if (st.ctrl_dead_frames < 100000) ++st.ctrl_dead_frames;
    }
    return st.ctrl_dead_frames;
}

static int 算法人物取Controller失联帧(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    if (it == 算法人物表.end()) return 0;
    if (!it->second.saw_controller) return 0;
    return it->second.ctrl_dead_frames;
}

static bool 算法人物曾有Controller(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    return it != 算法人物表.end() && it->second.saw_controller;
}

static int 算法人物记Mesh存活(uintptr_t actor, bool mesh_ok) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto& st = 算法人物表[actor];
    if (mesh_ok) {
        st.saw_mesh = true;
        st.mesh_dead_frames = 0;
    } else if (st.saw_mesh) {
        if (st.mesh_dead_frames < 100000) ++st.mesh_dead_frames;
    }
    return st.mesh_dead_frames;
}

static int 算法人物取Mesh失联帧(uintptr_t actor) {
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    if (it == 算法人物表.end() || !it->second.saw_mesh) return 0;
    return it->second.mesh_dead_frames;
}

// 退出残留：仅曾在世界数组且连续缺席够久（退局不在 ULevel，内存字段常仍活）
static bool 算法人物判定退出残留(uintptr_t actor, bool /*is_ai*/) {
    if (!是有效用户态地址(actor)) return false;
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    if (it == 算法人物表.end() || !it->second.saw_world) return false;
    return it->second.miss_world_frames >= 算法世界缺席快踢帧;
}

static bool 算法人物应停续命(uintptr_t actor) {
    if (!是有效用户态地址(actor)) return false;
    std::lock_guard<std::mutex> lock(确认帧锁);
    auto it = 算法人物表.find(actor);
    if (it == 算法人物表.end() || !it->second.saw_world) return false;
    return it->second.miss_world_frames >= 算法世界缺席停续命帧;
}

// TTL 到期：清确认表 + 短禁扫描再进（打断空气人回捞循环）
// 不用死亡黑名单：真人若只是短暂没续上，12s 后仍可再进，避免永久闪
static void 算法人物TTL到期处理(uintptr_t actor) {
    if (!是有效用户态地址(actor)) return;
    {
        std::lock_guard<std::mutex> lock(确认帧锁);
        算法人物表.erase(actor);
        算法TTL禁入表[actor] = std::chrono::steady_clock::now();
    }
}

static size_t 算法缓存人物数量() {
    std::lock_guard<std::mutex> lock(Actor池_线程锁);
    size_t n = 0;
    for (const auto& kv : 全局_自建Actor人物标记) {
        if (kv.second) ++n;
    }
    return n;
}

// 仅确认后 / 候选好帧 调用：续 TTL
static void 刷新算法缓存Actor(uintptr_t actor, bool isPlayer) {
    if (!是有效用户态地址(actor)) return;
    {
        std::lock_guard<std::mutex> lock(Actor池_线程锁);
        auto it = 全局_自建Actor缓存.find(actor);
        if (it == 全局_自建Actor缓存.end()) return;
        const auto now = std::chrono::steady_clock::now();
        it->second = now;
        全局_自建Actor人物标记[actor] = isPlayer;
        全局_自建Actor池更新时间 = now;
    }
    重置解密失败计数(actor);
}

static void 移除算法缓存Actor(uintptr_t actor) {
    if (!是有效用户态地址(actor)) return;
    // 锁序固定：Actor池 → 失败计数 → 确认帧（禁止反序，防死锁）
    {
        std::lock_guard<std::mutex> lock(Actor池_线程锁);
        全局_自建Actor缓存.erase(actor);
        全局_自建Actor人物标记.erase(actor);
        auto it = std::remove(全局_自建Actor池.begin(), 全局_自建Actor池.end(), actor);
        if (it != 全局_自建Actor池.end()) {
            全局_自建Actor池.erase(it, 全局_自建Actor池.end());
        }
    }
    {
        std::lock_guard<std::mutex> lock(失败计数锁);
        解密失败计数.erase(actor);
    }
    清除算法人物确认(actor);
}

// 假点/尸体踢出：进黑名单，扫描与冷启动一段时间内不让再进
static void 移除并拉黑算法人物(uintptr_t actor) {
    加入算法人物黑名单(actor);
    移除算法缓存Actor(actor);
}

// 已确认软移除：不拉黑（防真人被误踢后 20s 进不来造成「几秒没有」）
static void 移除算法人物不拉黑(uintptr_t actor) {
    移除算法缓存Actor(actor);
}

static void 延迟移除算法缓存Actor(uintptr_t actor) {
    if (!是有效用户态地址(actor)) return;
    const bool confirmed = 算法人物已确认(actor);
    if (!confirmed) {
        重置算法人物确认(actor);
    }
    int 当前;
    {
        std::lock_guard<std::mutex> lock(失败计数锁);
        当前 = ++解密失败计数[actor];
    }
    const int thr = confirmed ? 容忍失败阈值 : 候选失败阈值;
    if (当前 >= thr) {
        {
            std::lock_guard<std::mutex> lock(失败计数锁);
            解密失败计数.erase(actor);
        }
        // 候选失败满 → 拉黑；已确认失败满 → 只移除不拉黑（可被扫描立刻接回）
        if (!confirmed) 移除并拉黑算法人物(actor);
        else 移除算法人物不拉黑(actor);
    }
}

static inline bool 是类名标识符字符(char ch) {
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           ch == '_';
}

static bool 类名包含独立片段(const char* text, const char* token) {
    if (!text || !token || !token[0]) return false;

    const size_t token_len = strlen(token);
    const char* pos = text;
    while ((pos = strstr(pos, token)) != nullptr) {
        const char before = (pos == text) ? '\0' : *(pos - 1);
        const char after = *(pos + token_len);
        if (!是类名标识符字符(before) && !是类名标识符字符(after)) {
            return true;
        }
        pos += token_len;
    }
    return false;
}

static bool 是可信载具类名(const char* className) {
    if (!className || !className[0]) return false;

    if (strstr(className, "VH_") != nullptr) return true;
    if (strncmp(className, "Vehicle_", 8) == 0) return true;

    static const char* 载具关键词[] = {
        "Buggy", "Dacia", "UAZ", "Mirado", "CoupeRB", "Rony",
        "AquaRail", "PG117", "MiniBus", "LadaNiva", "Snowmobile",
        "Motorcycle", "Scooter", "Tuk", "StationWagon", "BRDM",
        "Blanc", "ATV", "SportCar", "Bigfoot", "Motorglider",
        "DesertCar"
    };

    for (const char* token : 载具关键词) {
        if (strstr(className, token) != nullptr) return true;
    }

    // 马和自行车用更严的片段匹配，避免把别的蓝图类误判出来。
    if (类名包含独立片段(className, "Horse_C")) return true;
    if (类名包含独立片段(className, "Bicycle")) return true;
    if (strstr(className, "Mountainbike") != nullptr || strstr(className, "MountainBike") != nullptr) return true;

    return false;
}

static bool 是可信玩家类名(const char* className) {
    if (!className || !className[0]) return false;
    if (strstr(className, "PlayerController") || strstr(className, "PlayerState") ||
        strstr(className, "Camera") || strstr(className, "Component")) {
        return false;
    }
    if (strstr(className, "STExtraBaseCharacter")) return true;
    if (strstr(className, "BPPawn")) return true;
    if (strstr(className, "BP_PlayerPawn") || strstr(className, "BP_PlayerCharacter")) return true;
    if (strstr(className, "PlayerCharacter")) return true;
    if (strstr(className, "Player") && (strstr(className, "Character") || strstr(className, "Pawn"))) return true;
    if (strstr(className, "Character") && strstr(className, "Pawn")) return true;
    return false;
}

static bool 是可信物资类名(const char* className) {
    if (!className || !className[0]) return false;
    if (strstr(className, "None") || strstr(className, "PlayerController") ||
        strstr(className, "PlayerState") || strstr(className, "Component") ||
        strstr(className, "Camera") || strstr(className, "Effect") ||
        strstr(className, "Particle") || strstr(className, "Niagara")) {
        return false;
    }

    if (是可信载具类名(className)) return true;
    if (strstr(className, "PickUp") || strstr(className, "Pickup") ||
        strstr(className, "Wrapper") || strstr(className, "WorldItem") ||
        strstr(className, "GroundItem")) return true;
    if (strstr(className, "InventoryBox") || strstr(className, "SupplyBox") ||
        strstr(className, "TreasureBox") || strstr(className, "AirDropBox") ||
        strstr(className, "TombBox") || strstr(className, "EscapeBox") ||
        strstr(className, "LootBox") || strstr(className, "DeadInventoryBox") ||
        strstr(className, "MilitarySupplyBox") || strstr(className, "RadiationBox") ||
        strstr(className, "MedicalBox") || strstr(className, "WeaponBox") ||
        strstr(className, "DTMedical") || strstr(className, "DTWeapon") ||
        strstr(className, "DTSupplyBox") || strstr(className, "DropPackage")) return true;
    if (strstr(className, "BP_Ammo_") || strstr(className, "BP_Item_") ||
        strstr(className, "BP_Neon_Coin") || strstr(className, "PeopleSkill")) return true;
    if (strstr(className, "BP_Pistol_") || strstr(className, "BP_Rifle_") ||
        strstr(className, "BP_Sniper_") || strstr(className, "BP_Shotgun_") ||
        strstr(className, "BP_SMG_") || strstr(className, "BP_LMG_") ||
        strstr(className, "BP_Crossbow_") || strstr(className, "BP_Melee_") ||
        strstr(className, "WeaponPickUp") || strstr(className, "FirearmPickup")) return true;
    // 信号枪/召回枪：地面类名含 Flaregun（RevivalFlaregun 亦含），不依赖 BP_ 前缀，确保过门
    if (strstr(className, "Flaregun")) return true;
    return false;
}

static const float 主题密钥显示距离 = 800.0f;
static const char* 获取主题密钥名称(const char* className, const 开关& 按钮);
static std::string 获取类名专项物资名(const char* className, 开关& 按钮);
static float 获取物资扫描距离上限(const 开关& 按钮);

void 启动无漏拾荒引擎(int 目标PID, class 绘制* draw_instance) {
    if (拾荒引擎运行中) {
        return;
    }

    拾荒引擎运行中 = true;

    std::thread([=]() {
        struct 引擎退出保护 {
            ~引擎退出保护() {
                std::lock_guard<std::mutex> lock(Actor池_线程锁);
                全局_自建Actor缓存.clear();
                全局_自建Actor人物标记.clear();
                全局_自建Actor池.clear();
                全局_自建Actor池更新时间 = {};
                拾荒引擎运行中 = false;
            }
        } 退出保护;

        try {
        uintptr_t lib_base = draw_instance->地址.libue4;
        
        // 🔧 增强版物资识别缓存
        struct CacheEntry {
            int identity;
            int fail_count;
        };
        static std::unordered_map<int, CacheEntry> ClassID_身份缓存池;
        
        // 🎯 核心大脑：实体身份鉴定器（和平精英专用优化版）
        auto 判定实体真实身份 = [](const char* ClassName) -> int {
            if (!ClassName || !ClassName[0]) return 0;
            
            // ========== 🔴 黑名单拦截（严格模式）==========
            // 寄生组件、特效、UI、相机等
            if (strstr(ClassName, "Component") || strstr(ClassName, "Effect") || 
                strstr(ClassName, "Camera") || strstr(ClassName, "UI_") || 
                strstr(ClassName, "Sound") || strstr(ClassName, "Decal") ||
                strstr(ClassName, "Wheel") || strstr(ClassName, "Seat") || 
                strstr(ClassName, "Tire") || strstr(ClassName, "HitBox") ||
                strstr(ClassName, "Helmet") || strstr(ClassName, "SceneCapture") ||
                strstr(ClassName, "Niagara") || strstr(ClassName, "Particle") ||
                strstr(ClassName, "Light") || strstr(ClassName, "StaticMesh") ||
                strstr(ClassName, "SkeletalMesh") || strstr(ClassName, "AnimInstance") ||
                strstr(ClassName, "Physics") || strstr(ClassName, "Collision") ||
                strstr(ClassName, "Movement") || strstr(ClassName, "Animation") ||
                strstr(ClassName, "GameMode") || strstr(ClassName, "GameState") ||
                strstr(ClassName, "PlayerController") || strstr(ClassName, "PlayerState")) return 0;
            
            // ========== ✅ 白名单1：玩家（严格匹配）==========
            if (strstr(ClassName, "STExtraBaseCharacter") || strstr(ClassName, "BPPawn_") || 
                strstr(ClassName, "PlayerCharacter") || strstr(ClassName, "Character") && strstr(ClassName, "Pawn")) return 1;
            
            // ========== ✅ 白名单2：盒子与容器（和平精英全类型）==========
            if (strstr(ClassName, "InventoryBox") || strstr(ClassName, "TreasureBox") || 
                strstr(ClassName, "SupplyBox") || strstr(ClassName, "AirDropBox") || 
                strstr(ClassName, "TombBox") || strstr(ClassName, "EscapeBox") || 
                strstr(ClassName, "DeadInventoryBox") || strstr(ClassName, "TrainingBox") || 
                strstr(ClassName, "PickUpListWrapperActor") || strstr(ClassName, "LootBox") || 
                strstr(ClassName, "CharacterDeadInventoryBox") || strstr(ClassName, "RollTombBox") || 
                strstr(ClassName, "EscapePlayerTombBox") || strstr(ClassName, "MilitarySupplyBox") ||
                strstr(ClassName, "DTInventoryBox") || strstr(ClassName, "DTSupplyBox") ||
                strstr(ClassName, "RadiationBox") || strstr(ClassName, "MedicalBox") ||
                strstr(ClassName, "WeaponBox") || strstr(ClassName, "DTWeapon") ||
                strstr(ClassName, "DTMedical") || strstr(ClassName, "DropPackage")) return 2;
            
            // ========== ✅ 白名单3：地面物资（大幅扩展和平精英类型）==========
            // 标准拾取物前缀
            if (strstr(ClassName, "PickUp_") || strstr(ClassName, "BP_Ammo_") || 
                strstr(ClassName, "BP_Neon_Coin") || strstr(ClassName, "Pickup_")) return 2;
            
            // 武器类
            if (strstr(ClassName, "BP_Pistol_") || strstr(ClassName, "BP_Rifle_") ||
                strstr(ClassName, "BP_Sniper_") || strstr(ClassName, "BP_Shotgun_") ||
                strstr(ClassName, "BP_SMG_") || strstr(ClassName, "BP_LMG_") ||
                strstr(ClassName, "BP_Crossbow_") || strstr(ClassName, "BP_Melee_") ||
                strstr(ClassName, "WeaponPickUp") || strstr(ClassName, "FirearmPickup")) return 2;
            
            // 弹药类
            if (strstr(ClassName, "BP_AmmoBox_") || strstr(ClassName, "AmmoPickup") ||
                strstr(ClassName, "AmmoBox") || strstr(ClassName, "BulletPickup")) return 2;
            
            // 药品与消耗品
            if (strstr(ClassName, "BP_Heal_") || strstr(ClassName, "BP_Boost_") ||
                strstr(ClassName, "BP_Medic_") || strstr(ClassName, "MedKit") ||
                strstr(ClassName, "FirstAid") || strstr(ClassName, "Bandage") ||
                strstr(ClassName, "EnergyDrink") || strstr(ClassName, "PainKiller") ||
                strstr(ClassName, "Adrenaline") || strstr(ClassName, "Syringe") ||
                strstr(ClassName, "ConsumablePickUp") || strstr(ClassName, "HealingItem")) return 2;
            
            // 防具类
            if (strstr(ClassName, "BP_Armor_") || strstr(ClassName, "BP_Helmet_") ||
                strstr(ClassName, "BP_Vest_") || strstr(ClassName, "BP_Bag_") ||
                strstr(ClassName, "ArmorPickUp") || strstr(ClassName, "HelmetPickUp") ||
                strstr(ClassName, "VestPickUp") || strstr(ClassName, "BackpackPickUp") ||
                strstr(ClassName, "PlateCarrier") || strstr(ClassName, "BodyArmor")) return 2;
            
            // 配件类
            if (strstr(ClassName, "BP_Scope_") || strstr(ClassName, "BP_Magazine_") ||
                strstr(ClassName, "BP_Grip_") || strstr(ClassName, "BP_Stock_") ||
                strstr(ClassName, "BP_Muzzle_") || strstr(ClassName, "BP_Bolt_") ||
                strstr(ClassName, "AttachmentPickUp") || strstr(ClassName, "ScopePickup") ||
                strstr(ClassName, "WeaponMod") || strstr(ClassName, "Accessory")) return 2;
            
            // 投掷物与特殊道具（仅匹配拾取物，排除飞行中的弹道）
            if (strstr(ClassName, "Grenade_Pickup") || strstr(ClassName, "Grenade_Wrapper") ||
                strstr(ClassName, "Smoke_Pickup") || strstr(ClassName, "Smoke_Wrapper") ||
                strstr(ClassName, "Molotov_Pickup") || strstr(ClassName, "Molotov_Wrapper") ||
                strstr(ClassName, "C4_Pickup") || strstr(ClassName, "C4_Wrapper") ||
                strstr(ClassName, "Stun_Pickup") || strstr(ClassName, "Stun_Wrapper") ||
                strstr(ClassName, "Trap") || strstr(ClassName, "Flare") ||
                strstr(ClassName, "SignalFlare") || strstr(ClassName, "Pistol_Flaregun") ||
                strstr(ClassName, "RevivalFlaregun") || strstr(ClassName, "Throwable")) return 2;
            
            // 通用物品类
            if (strstr(ClassName, "BP_Item_") || strstr(ClassName, "EquipPickUp") ||
                strstr(ClassName, "WorldItem") || strstr(ClassName, "GroundItem")) return 2;
            
            // ========== ✅ 白名单4：载具（完整列表）==========
            if (是可信载具类名(ClassName)) return 2;

            return 0;
        };
        
        // FName安全读取
        auto 安全获取ClassName = [&](int obj_id, char* out_buffer, size_t buf_size) -> bool {
            try {
                uintptr_t gnames_base = draw_instance->地址.类地址;
                
                long int FNameEntry = draw_instance->读写.getPtr64(
                    draw_instance->读写.getPtr64(gnames_base + (obj_id / 0x4000) * 0x8) + 
                    (obj_id % 0x4000) * 0x8
                );
                
                if (FNameEntry > 0x1000000000 && FNameEntry < 0x8000000000) {
                    memset(out_buffer, 0, buf_size);
                    if (draw_instance->读写.readv(FNameEntry + 0xC, out_buffer, buf_size)) {
                        if (out_buffer[0] != '\0') return true;
                    }
                }
                
                // 备用偏移
                long int FNameEntryAlt = draw_instance->读写.getPtr64(
                    draw_instance->读写.getPtr64(gnames_base + (obj_id / 0x4000) * 0x8) + 
                    (obj_id % 0x4000) * 0x8 + 0x10
                );
                
                if (FNameEntryAlt > 0x1000000000 && FNameEntryAlt < 0x8000000000) {
                    memset(out_buffer, 0, buf_size);
                    if (draw_instance->读写.readv(FNameEntryAlt + 0x10, out_buffer, buf_size)) {
                        if (out_buffer[0] != '\0') return true;
                    }
                }
            } catch (...) {}
            return false;
        };

        while (draw_instance->按钮.算法解密开关 || draw_instance->按钮.物资解密开关) {
            std::vector<uintptr_t> 临时淘金池;
            std::vector<uintptr_t> 临时人物池;
            std::vector<HookRW::MemSeg> segments = 获取内存段(目标PID);

            int 自身队伍ID = draw_instance->自身数据.自身队伍;
            int 物资识别计数 = 0;
            const int 最大物资数量 = 500;
            // 大堆段不能整段丢：>2MB 的 rw-p 里常有真人 Actor。按块扫。
            constexpr size_t 单块扫描字节 = 0x200000;
            constexpr size_t 最小段字节 = 0x10000;

            for (const auto& seg : segments) {
                size_t total = seg.end - seg.start;
                if (total < 最小段字节) continue;

                thread_local static std::vector<uint8_t> 本地缓冲块;
                for (size_t chunk_off = 0; chunk_off < total; chunk_off += 单块扫描字节) {
                    size_t size = std::min(单块扫描字节, total - chunk_off);
                    if (size < 最小段字节) continue;
                    const uintptr_t chunk_start = seg.start + chunk_off;

                try {
                    if (本地缓冲块.capacity() < size) {
                        本地缓冲块.reserve(size);
                    }
                    本地缓冲块.resize(size);
                } catch (const std::bad_alloc&) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    continue;
                } catch (...) {
                    continue;
                }

                if (draw_instance->读写.readv(chunk_start, 本地缓冲块.data(), size)) {
                    // 块内从 0x1000 起扫，末尾留 0x4000 防越界读字段
                    size_t scan_end = (size > 0x5000) ? (size - 0x4000) : 0;
                    for (size_t offset = 0x1000; offset < scan_end; offset += 16) {
                        uint8_t* actor_base = 本地缓冲块.data() + offset - 0x1000;

                        float magic_val = *(float*)(actor_base + 0x3b9c);
                        if (draw_instance->按钮.算法解密开关 && abs(magic_val - 479.5f) < 0.1f) {
                            // 进池收紧：479.5 + 有效队 + 真名指针 + Root/Mesh + 未死
                            // 上一版只卡 479.5/Mesh，全图假点狂进 → 空气人一下一大片
                            uintptr_t 真实Actor = (chunk_start + offset) - 0x1000;
                            if (!是有效用户态地址(真实Actor)) {
                                continue;
                            }
                            if (算法人物禁止新进(真实Actor)) {
                                continue;
                            }
                            // 退出残留可能仍有真名/血/479.5；进池前按真实离册签名硬挡。
                            const uint64_t exit_lifecycle =
                                *(uint64_t*)(actor_base + 算法退出生命周期偏移);
                            const uintptr_t exit_owner_a =
                                *(uintptr_t*)(actor_base + 算法退出归属链A偏移);
                            const uintptr_t exit_owner_b =
                                *(uintptr_t*)(actor_base + 算法退出归属链B偏移);
                            if (是算法退出残留签名(exit_lifecycle,
                                                     exit_owner_a,
                                                     exit_owner_b)) {
                                加入算法人物黑名单(真实Actor);
                                continue;
                            }

                            int team_id = *(int*)(actor_base + 0xB78);
                            if (!队伍编号有效(team_id)) {
                                continue;
                            }
                            if (队伍编号有效(自身队伍ID) && team_id == 自身队伍ID) {
                                continue;
                            }

                            int obj_id = *(int*)(actor_base + 0x18);
                            if (obj_id > 0 && obj_id <= 3000000) {
                                char PlayerClassName[128] = {0};
                                if (安全获取ClassName(obj_id, PlayerClassName, sizeof(PlayerClassName))) {
                                    if (strstr(PlayerClassName, "PlayerController") ||
                                        strstr(PlayerClassName, "PlayerState") ||
                                        strstr(PlayerClassName, "PlayerCamera") ||
                                        strstr(PlayerClassName, "Widget") ||
                                        strstr(PlayerClassName, "AnimInstance") ||
                                        strstr(PlayerClassName, "AnimBlueprint") ||
                                        strstr(PlayerClassName, "Component") ||
                                        strstr(PlayerClassName, "SceneCapture")) {
                                        continue;
                                    }
                                }
                            }

                            uintptr_t root_comp = *(uintptr_t*)(actor_base + 0x260);
                            uintptr_t mesh_ptr = *(uintptr_t*)(actor_base + 0x658);
                            uintptr_t name_ptr = *(uintptr_t*)(actor_base + 0xAF8);
                            if (!是有效用户态地址(root_comp) ||
                                !是有效用户态地址(mesh_ptr) ||
                                !是有效用户态地址(name_ptr)) {
                                continue;
                            }
                            // 扫侧读名：必须像真人名；堆上随机串不进池
                            {
                                char nbuf[48] = {0};
                                draw_instance->读写.getUTF8(nbuf, name_ptr);
                                if (!是有效玩家名(std::string(nbuf))) {
                                    continue;
                                }
                            }

                            float scan_hp = *(float*)(actor_base + 0x1060);
                            float scan_hpmax = *(float*)(actor_base + 0x1068);
                            // 合理活血：假点 Health 常 NaN/0/天文，这是空气人第二入口
                            if (!是合理人物血(scan_hp, scan_hpmax, false)) {
                                continue;
                            }
                            uintptr_t scan_state_ptr = *(uintptr_t*)(actor_base + 0x1700);
                            if (是有效用户态地址(scan_state_ptr)) {
                                int scan_st = 0;
                                if (draw_instance->读写.readv(scan_state_ptr, &scan_st, sizeof(scan_st))) {
                                    if (scan_st == 1048592 || scan_st == 1048576) {
                                        continue;
                                    }
                                }
                            }
                            {
                                float ppos[3] = {0};
                                if (!draw_instance->读写.readv(root_comp + 0x200, &ppos, sizeof(ppos)) ||
                                    !std::isfinite(ppos[0]) || !std::isfinite(ppos[1]) || !std::isfinite(ppos[2]) ||
                                    (std::abs(ppos[0]) < 0.001f && std::abs(ppos[1]) < 0.001f && std::abs(ppos[2]) < 0.001f) ||
                                    std::abs(ppos[0]) > 9000000.0f || std::abs(ppos[1]) > 9000000.0f ||
                                    std::abs(ppos[2]) > 9000000.0f) {
                                    continue;
                                }
                                float pdx = ppos[0] - draw_instance->自身数据.坐标.X;
                                float pdy = ppos[1] - draw_instance->自身数据.坐标.Y;
                                float pdz = ppos[2] - draw_instance->自身数据.坐标.Z;
                                float pdist = sqrtf(pdx * pdx + pdy * pdy + pdz * pdz) * 0.01f;
                                float pcap = (float)draw_instance->按钮.绘制最大距离 + 30.0f;
                                if (pcap < 120.0f) pcap = 120.0f;
                                if (pdist > pcap) continue;
                                // 贴脸 0.5m 内多半自身残影
                                if (pdist < 0.5f) continue;
                            }

                            临时淘金池.push_back(真实Actor);
                            临时人物池.push_back(真实Actor);
                            continue;
                        }
                        
                        if (!draw_instance->按钮.物资解密开关) continue;

                        if (物资识别计数 >= 最大物资数量) break;

                        uintptr_t vtable = *(uintptr_t*)actor_base;
                        if (!是libUE4模块地址(vtable, lib_base, draw_instance->pid)) continue;

                        int obj_id = *(int*)(actor_base + 0x18);
                        if (obj_id <= 0 || obj_id > 3000000) continue;

                        uintptr_t root_comp = *(uintptr_t*)(actor_base + 0x260);
                        if (root_comp < 0x1000000000 || root_comp > 0x8000000000) continue;

                        uintptr_t 真实Actor = (chunk_start + offset) - 0x1000;

                        // ★ 算法解密1 手雷预警补丁 ★
                        // 拾荒引擎原来只收玩家/物资/盒子/车辆，手雷投掷物不是物资会被后面 ClassName 身份过滤掉。
                        // 这里先用 actor+0x794 识别投掷物，命中 Getagrenade() 就直接进缓存，
                        // 让算法解密1也能在主循环里执行原版手雷倒计时/网格逻辑。
                        if (draw_instance->按钮.手雷预警) {
                            int 投掷物ID = *(int*)(actor_base + 0x794);
                            if (Getagrenade(投掷物ID) != nullptr) {
                                float obj_pos[3] = {0};
                                if (draw_instance->读写.readv(root_comp + 0x200, &obj_pos, sizeof(obj_pos)) &&
                                    std::isfinite(obj_pos[0]) && std::isfinite(obj_pos[1]) && std::isfinite(obj_pos[2]) &&
                                    !(std::abs(obj_pos[0]) < 0.001f && std::abs(obj_pos[1]) < 0.001f && std::abs(obj_pos[2]) < 0.001f)) {
                                    临时淘金池.push_back(真实Actor);
                                    物资识别计数++;
                                    continue;
                                }
                            }
                        }

                        // 🔧 修复：使用地址作为缓存键，不用obj_id
                        // 每次扫描都重新验证，不依赖旧的缓存
                        char ClassName[128] = {0};
                        if (!安全获取ClassName(obj_id, ClassName, sizeof(ClassName))) continue;

                        int 实体身份 = 判定实体真实身份(ClassName);
                        if (实体身份 != 2) continue;

                        // 🔧 修复：验证物资位置是否有效，无效则跳过
                        float obj_pos[3] = {0};
                        if (!draw_instance->读写.readv(root_comp + 0x200, &obj_pos, sizeof(obj_pos))) continue;
                        
                        // 检查坐标是否合理（防止读取到垃圾值）
                        if (std::abs(obj_pos[0]) < 0.001f && std::abs(obj_pos[1]) < 0.001f && std::abs(obj_pos[2]) < 0.001f) continue;
                        
                        float dx = obj_pos[0] - draw_instance->自身数据.坐标.X;
                        float dy = obj_pos[1] - draw_instance->自身数据.坐标.Y;
                        float dz = obj_pos[2] - draw_instance->自身数据.坐标.Z;
                        float distance = sqrt(dx*dx + dy*dy + dz*dz) / 100.0f;
                        float 扫描距离上限 = 获取物资扫描距离上限(draw_instance->按钮) + 200.0f;
                        if (distance > 扫描距离上限) continue;
                        
                        临时淘金池.push_back(真实Actor);
                        物资识别计数++;
                    }
                }
                } // chunk_off
            } // seg

            static int debug_counter = 0;
            if (debug_counter++ % 120 == 0 && 物资识别计数 > 0) {
                printf("\033[36m[DEBUG] 本轮扫描发现 %d 个物资目标，池大小=%zu\033[0m\n",
                       物资识别计数, 临时淘金池.size());
            }

            if (!临时淘金池.empty()) {
                std::sort(临时淘金池.begin(), 临时淘金池.end());
                临时淘金池.erase(std::unique(临时淘金池.begin(), 临时淘金池.end()), 临时淘金池.end());
            }

            {
                std::lock_guard<std::mutex> lock(Actor池_线程锁);
                const auto now = std::chrono::steady_clock::now();
                // 人物上限压紧：假点再多也只留一小撮
                constexpr size_t kMaxPlayerCache = 64;
                constexpr size_t kMaxActorCacheSize = 512;
                size_t playerCount = 0;
                for (const auto& kv : 全局_自建Actor人物标记) {
                    if (kv.second) ++playerCount;
                }

                for (uintptr_t actor : 临时淘金池) {
                    if (!是有效用户态地址(actor)) continue;
                    auto existing = 全局_自建Actor缓存.find(actor);
                    if (existing != 全局_自建Actor缓存.end()) {
                        // 物资已在池：允许续扫刷新；人物必须靠绘制强校验续命
                        auto mark_it = 全局_自建Actor人物标记.find(actor);
                        if (mark_it == 全局_自建Actor人物标记.end() || !mark_it->second) {
                            existing->second = now;
                            全局_自建Actor人物标记[actor] = false;
                        }
                        continue;
                    }
                    if (全局_自建Actor缓存.size() >= kMaxActorCacheSize) break;
                    全局_自建Actor缓存[actor] = now;
                    全局_自建Actor人物标记[actor] = false;
                }
                for (uintptr_t actor : 临时人物池) {
                    if (!是有效用户态地址(actor)) continue;
                    auto existing = 全局_自建Actor缓存.find(actor);
                    if (existing != 全局_自建Actor缓存.end()) {
                        // 已在池的人物：扫描只刷新标记，不刷新 TTL（防假点续命）
                        全局_自建Actor人物标记[actor] = true;
                        continue;
                    }
                    if (playerCount >= kMaxPlayerCache) continue;
                    if (全局_自建Actor缓存.size() >= kMaxActorCacheSize) break;
                    全局_自建Actor缓存[actor] = now;
                    全局_自建Actor人物标记[actor] = true;
                    ++playerCount;
                }

                for (auto it = 全局_自建Actor缓存.begin(); it != 全局_自建Actor缓存.end();) {
                    bool is_player_cache = false;
                    auto mark_it = 全局_自建Actor人物标记.find(it->first);
                    if (mark_it != 全局_自建Actor人物标记.end()) {
                        is_player_cache = mark_it->second;
                    }
                    // 已确认人物长 TTL，候选短 TTL
                    int ttl = 算法物资缓存有效秒;
                    if (is_player_cache) {
                        ttl = 算法人物已确认(it->first) ? 算法人物确认有效秒 : 算法人物候选有效秒;
                    }
                    if (now - it->second > std::chrono::seconds(ttl)) {
                        uintptr_t dead = it->first;
                        const bool was_player = is_player_cache;
                        全局_自建Actor人物标记.erase(dead);
                        it = 全局_自建Actor缓存.erase(it);
                        // 持 Actor 锁下可调（只抢确认锁）；冻结残留顺带拉黑
                        if (was_player) 算法人物TTL到期处理(dead);
                    } else {
                        ++it;
                    }
                }

                全局_自建Actor池.clear();
                全局_自建Actor池.reserve(全局_自建Actor缓存.size());
                for (const auto& item : 全局_自建Actor缓存) {
                    全局_自建Actor池.push_back(item.first);
                }

                if (!临时淘金池.empty() || !临时人物池.empty()) {
                    全局_自建Actor池更新时间 = now;
                } else if (全局_自建Actor池.empty()) {
                    全局_自建Actor池更新时间 = {};
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }

        ClassID_身份缓存池.clear();
        } catch (const std::exception&) {
        } catch (...) {
        }

    }).detach();
}

// ========== 算法解密相关代码结束 ==========

// 修复：正确的参数声明语法
int 验证内存(class 绘制 *绘制实例, uintptr_t addr)
{
    int count = 0;
    for (uintptr_t p = addr - 4;; p -= 8)
    {
        int value = 绘制实例->读写.getDword(p);
        if (value < 100 || value > 200)
            break;
        count++;
    }
    return count;
}

// 修复：正确的参数声明语法
std::vector<uintptr_t> 扫描内存段(const HookRW::MemSeg &seg, uintptr_t target, class 绘制 *绘制实例)
{
    std::vector<uintptr_t> results;

    // 按4KB页边界对齐
    uintptr_t start = (seg.start + 0xFFF) & ~0xFFF;
    uintptr_t end = seg.end & ~0xFFF;
    if (start >= end)
        return results;

    const size_t pageSize = 0x1000;
    const size_t ptrSize = 8;
    uint8_t *buffer = new uint8_t[pageSize];
    if (!buffer)
        return results;

    // 分页读取并扫描
    for (uintptr_t offset = 0; offset + pageSize <= end - start; offset += pageSize)
    {
        if (!绘制实例->读写.readv(start + offset, buffer, pageSize))
        {
            continue; // 读取失败则跳过
        }

        for (size_t i = 0; i + ptrSize <= pageSize; i += ptrSize)
        {
            uintptr_t value = *reinterpret_cast<uintptr_t *>(buffer + i);
            if (value == target)
            {
                results.push_back(start + offset + i);
            }
        }
    }

    delete[] buffer;
    return results;
}



#endif



