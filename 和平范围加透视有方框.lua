--[[
============================================================
  和平精英 · 绿色射线透视 + 10.0 范围 · 液态玻璃 UI
  单文件版
  ------------------------------------------------------------
  环境：安卓 Lua 脚本引擎（蜂窝 / 触动 / GG 衍生）
  ------------------------------------------------------------
  新增：
    - 进入游戏自动弹出方框
    - 音量上键隐藏方框
    - 音量下键弹出方框
    - UI 使用液态玻璃质感（磨砂半透明 + 高光 + 流动反光）
============================================================
]]

-- ==================== 引擎 API 别名 ====================
local D = {
    rect      = draw.rect,
    roundRect = draw.roundRect or draw.rect,
    text      = draw.text,
    line      = draw.line,
    circle    = draw.circle,
    color     = draw.setColor or function() end,
    width     = function() return draw.getWidth()  end,
    height    = function() return draw.getHeight() end,
}
local Touch = touch
local KeyNS = key or keys or keycode
local CFG_FILE = "pubg_green_esp.cfg"

-- 音量键码
local VK_UP   = 24
local VK_DOWN = 25

-- ==================== 硬编码主题 ====================
local GREEN_LINE = { 0.10, 1.00, 0.35, 1.00 }
local GREEN_BOX  = { 0.10, 1.00, 0.35, 0.85 }
local GREEN_FILL = { 0.10, 1.00, 0.35, 0.12 }

local T = {
    text   = {0.95, 0.97, 1.00, 1.00},
    dim    = {0.62, 0.66, 0.72, 1.00},
    accent = {0.10, 1.00, 0.35, 1.00},
    track  = {0.85, 0.90, 1.00, 0.18},
    line   = {0.85, 0.90, 1.00, 0.22},
}

-- ==================== 固定参数 ====================
local RANGE_RADIUS = 10.0

-- ==================== 运行状态 ====================
local S = {
    x = 30, y = 110,
    w = 290, h = 400,
    open = true,
    visible = false,          -- 方框是否显示（音量键控制）
    autoPopped = false,       -- 进游戏是否已自动弹出
    tab = 1,
    dragging = false,
    dragX = 0, dragY = 0,
    rowH = 34,
    anim = {},
    tick = 0,
}

local TABS = { "透视", "范围", "设置" }

-- ==================== 功能开关 ====================
local CFG = {
    espLine   = true,
    espBox    = true,
    espName   = true,
    espDist   = true,
    espHealth = true,
    maxDist   = 300,
    linePos   = 1,
    rangeOn      = true,
    rangeAttack  = true,
    rangePick    = false,
    rangeCircle  = true,
    safeMode  = true,
    fontScale = 1.0,
    pxPerUnit = 9.5,
}

-- ==================== 配置持久化 ====================
local function saveCfg()
    local f = io.open(CFG_FILE, "w")
    if not f then return end
    f:write("return {\n")
    for k, v in pairs(CFG) do
        if type(v) == "boolean" then
            f:write(string.format("  %s = %s,\n", k, tostring(v)))
        elseif type(v) == "table" then
            f:write(string.format("  %s = {%s,%s,%s,%s},\n", k, v[1],v[2],v[3],v[4]))
        else
            f:write(string.format("  %s = %s,\n", k, tostring(v)))
        end
    end
    f:write("}\n")
    f:close()
end

local function loadCfg()
    local f = io.open(CFG_FILE, "r")
    if not f then return end
    local src = f:read("*a")
    f:close()
    local ok, t = pcall(load or loadstring, src)
    if ok and type(t) == "table" then
        for k, v in pairs(t) do CFG[k] = v end
    end
end

-- ==================== 绘制工具 ====================
local function setC(c) D.color(c[1], c[2], c[3], c[4] or 1) end

local function txt(x, y, s, c, size)
    setC(c or T.text)
    D.text(x, y, s, size or 12)
end

-- ==================== 液态玻璃层 ====================
-- 由多层半透明填充 + 顶部高光带 + 边框描边 + 流动反光组成
-- 所有坐标使用 (x1,y1,x2,y2) 对，与引擎一致

local function glassPanel(x1, y1, x2, y2, r)
    local w, h = x2 - x1, y2 - y1

    -- 1. 底色：冷调磨砂
    setC({0.42, 0.48, 0.58, 0.26})
    D.roundRect(x1, y1, x2, y2, r)

    -- 2. 内层提亮：玻璃本体的乳白感
    setC({0.80, 0.86, 0.96, 0.10})
    D.roundRect(x1 + 1.5, y1 + 1.5, x2 - 1.5, y2 - 1.5, math.max(1, r - 1))

    -- 3. 顶部高光带：模拟光从上方打进来
    local bands = 7
    for i = 1, bands do
        local t = i / bands
        setC({1, 1, 1, 0.075 * (1 - t) * (1 - t)})
        local by1 = y1 + 2 + (h - 4) * (i - 1) / bands
        local by2 = y1 + 2 + (h - 4) * i / bands
        D.rect(x1 + 3, by1, x2 - 3, by2)
    end

    -- 4. 底部内阴影
    setC({0.05, 0.08, 0.14, 0.16})
    D.roundRect(x1 + 2, y2 - h * 0.35, x2 - 2, y2 - 1, math.max(1, r - 2))

    -- 5. 外描边：亮边
    setC({0.88, 0.94, 1.00, 0.30})
    D.roundRect(x1, y1, x2, y2, r)
    -- 6. 内描边：暗边，制造厚度
    setC({0.10, 0.14, 0.22, 0.20})
    D.roundRect(x1 + 1, y1 + 1, x2 - 1, y2 - 1, math.max(1, r - 1))

    -- 7. 顶部镜面细线
    setC({1, 1, 1, 0.45})
    D.line(x1 + r * 0.6, y1 + 1, x2 - r * 0.6, y1 + 1)

    -- 8. 流动反光：沿面板横向扫过的一束高光
    local phase = (S.tick % 180) / 180
    local sweep = x1 - w * 0.4 + (w * 1.8) * phase
    local sheenW = w * 0.18
    for i = 1, 4 do
        local a = 0.030 * (1 - math.abs(i - 2.5) / 2.5)
        setC({1, 1, 1, a})
        D.rect(
            math.max(x1 + 2, sweep - sheenW / 2 + (i - 1) * sheenW / 4),
            y1 + 2,
            math.min(x2 - 2, sweep - sheenW / 2 + i * sheenW / 4),
            y2 - 2
        )
    end
end

-- 玻璃小控件底（开关轨道、分段按钮）
local function glassChip(x1, y1, x2, y2, r, active)
    if active then
        setC({0.10, 1.00, 0.35, 0.72})
        D.roundRect(x1, y1, x2, y2, r)
        setC({1, 1, 1, 0.30})
        D.line(x1 + r * 0.5, y1 + 1, x2 - r * 0.5, y1 + 1)
        setC({0.02, 0.22, 0.10, 0.30})
        D.roundRect(x1 + 1, y1 + 1, x2 - 1, y2 - 1, math.max(1, r - 1))
    else
        setC({0.55, 0.62, 0.74, 0.22})
        D.roundRect(x1, y1, x2, y2, r)
        setC({1, 1, 1, 0.16})
        D.line(x1 + r * 0.5, y1 + 1, x2 - r * 0.5, y1 + 1)
        setC({0.10, 0.14, 0.22, 0.18})
        D.roundRect(x1 + 1, y1 + 1, x2 - 1, y2 - 1, math.max(1, r - 1))
    end
end

-- ==================== 控件 ====================
local function toggle(key, x, y, w, label)
    local prog = S.anim[key] or (CFG[key] and 1 or 0)
    txt(x, y + 8, label, T.text, 12)
    local tx, ty = x + w - 44, y + 9
    local tw, th = 38, 18
    glassChip(tx, ty, tx + tw, ty + th, th / 2, prog > 0.5)
    setC({0.98, 0.99, 1.00, 1.00})
    local kx = tx + 2 + (tw - th) * prog
    D.circle(kx + th / 2 - 2, ty + th / 2, th / 2 - 2)
    setC({1, 1, 1, 0.55})
    D.circle(kx + th / 2 - 2, ty + th / 2 - 2, th / 6)
end

local function slider(key, x, y, w, label, min, max)
    local v = CFG[key]
    local p = (v - min) / (max - min)
    txt(x, y + 4, label, T.text, 12)
    txt(x + w - 46, y + 4, tostring(math.floor(v)), T.dim, 12)
    local tx, ty, tw, th = x, y + 20, w, 5
    glassChip(tx, ty, tx + tw, ty + th, th / 2, false)
    if p > 0.01 then
        glassChip(tx, ty, tx + tw * p, ty + th, th / 2, true)
    end
    setC({1, 1, 1, 0.95})
    D.circle(tx + tw * p, ty + th / 2, 6)
    setC({1, 1, 1, 0.40})
    D.circle(tx + tw * p, ty + th / 2 - 1.5, 2.5)
end

local function seg(key, x, y, w, label, opts)
    txt(x, y + 4, label, T.text, 12)
    local n = #opts
    local bw = (w - 8) / n
    for i, o in ipairs(opts) do
        local bx = x + (i - 1) * bw
        local sel = CFG[key] == i
        glassChip(bx, y + 18, bx + bw - 4, y + 38, 6, sel)
        txt(bx + 6, y + 22, o, sel and {0.04,0.20,0.10,1} or T.dim, 11)
    end
end

-- ==================== 命中检测 ====================
local function hit(mx, my, x, y, w, h)
    return mx >= x and mx <= x + w and my >= y and my <= y + h
end

-- ==================== 数据接口 ====================
if not getEntities then
    function getEntities() return {} end
end
if not getSelfWorld then
    function getSelfWorld() return nil end
end

-- ==================== 透视渲染 ====================
local function renderESP()
    local list = getEntities()
    if not list then return end
    local sw, sh = D.width(), D.height()

    for _, e in ipairs(list) do
        if e.dist <= CFG.maxDist then
            if CFG.espBox then
                setC(GREEN_BOX)
                D.rect(e.sx, e.sy, e.sx + e.sw, e.sy + e.sh)
            end
            if CFG.espLine then
                local fromX, fromY = sw / 2, sh
                if CFG.linePos == 2 then fromY = sh / 2
                elseif CFG.linePos == 3 then fromY = 0 end
                setC(GREEN_LINE)
                D.line(fromX, fromY, e.sx + e.sw / 2, e.sy + e.sh)
            end
            if CFG.espHealth then
                local hpc = {0.9, 0.2, 0.2, 1}
                if e.hp > 50 then hpc = {0.9, 0.75, 0.15, 1} end
                if e.hp > 75 then hpc = {0.2, 0.85, 0.35, 1} end
                setC({1, 1, 1, 0.18})
                D.rect(e.sx - 6, e.sy, e.sx - 3, e.sy + e.sh)
                setC(hpc)
                D.rect(e.sx - 6, e.sy + e.sh * (1 - e.hp / 100), e.sx - 3, e.sy + e.sh)
            end
            if CFG.espName and e.name then
                txt(e.sx, e.sy - 16, e.name, GREEN_LINE, 11)
            end
            if CFG.espDist then
                txt(e.sx, e.sy + e.sh + 2, tostring(math.floor(e.dist)) .. "m", GREEN_LINE, 11)
            end
        end
    end
end

-- ==================== 范围功能 ====================
local function inRange(a, b, r)
    local dx = a.wx - b.wx
    local dy = a.wy - b.wy
    return (dx * dx + dy * dy) <= (r * r)
end

local function renderRange()
    if not CFG.rangeOn then return end
    local self = getSelfWorld()
    if not self then return end

    if CFG.rangeCircle then
        local rpx = RANGE_RADIUS * (CFG.pxPerUnit or 9.5)
        setC(GREEN_FILL)
        D.circle(self.sx, self.sy, rpx)
        setC(GREEN_LINE)
        D.circle(self.sx, self.sy, rpx)
    end

    local list = getEntities()
    if not list then return end
    for _, e in ipairs(list) do
        if e.wx and e.wy and inRange(self, e, RANGE_RADIUS) then
            setC(GREEN_LINE)
            D.rect(e.sx - 2, e.sy - 2, e.sx + e.sw + 2, e.sy + e.sh + 2)
            if CFG.rangeAttack and onRangeTarget then onRangeTarget(e) end
            if CFG.rangePick and onRangePick then onRangePick(e) end
        end
    end
end

-- ==================== 面板渲染（液态玻璃） ====================
local function renderPanel()
    if not S.visible then return end

    local x, y, w, h = S.x, S.y, S.w, S.h

    if not S.open then
        glassPanel(x, y, x + 100, y + 34, 12)
        txt(x + 14, y + 10, "ESP ▶", T.accent, 12)
        return
    end

    -- 整块玻璃底板
    glassPanel(x, y, x + w, y + h, 14)

    -- 标题栏（独立一层薄玻璃）
    glassPanel(x + 6, y + 6, x + w - 6, y + 34, 10)
    txt(x + 16, y + 13, "PUBG · 绿透 + 10.0 范围", T.text, 12)
    txt(x + w - 28, y + 13, "—", T.dim, 14)

    -- 标签页
    local tw = w / #TABS
    for i, name in ipairs(TABS) do
        local tx = x + (i - 1) * tw
        local sel = (S.tab == i)
        if sel then
            glassChip(tx + 10, y + 42, tx + tw - 10, y + 66, 8, true)
        end
        txt(tx + (tw - #name * 12) / 2, y + 48, name,
            sel and {0.04,0.20,0.10,1} or T.dim, 12)
    end

    -- 分隔线
    setC(T.line)
    D.line(x + 14, y + 74, x + w - 14, y + 74)

    local by = y + 86
    local rowW = w - 28
    local rx = x + 14
    local oy = 0

    if S.tab == 1 then
        local keys = {
            {"espLine","绿色射线"}, {"espBox","绿色方框"},
            {"espName","玩家昵称"}, {"espDist","距离显示"},
            {"espHealth","血量显示"},
        }
        for _, kv in ipairs(keys) do
            toggle(kv[1], rx, by + oy, rowW, kv[2])
            oy = oy + S.rowH
        end
        slider("maxDist", rx, by + oy + 4, rowW, "透视最大距离", 50, 1000)
        oy = oy + 44
        seg("linePos", rx, by + oy, rowW, "射线起点", {"底部","中心","顶部"})

    elseif S.tab == 2 then
        toggle("rangeOn",     rx, by + oy, rowW, "范围功能总开关"); oy = oy + S.rowH
        toggle("rangeCircle", rx, by + oy, rowW, "屏幕范围圈");     oy = oy + S.rowH
        toggle("rangeAttack", rx, by + oy, rowW, "范围内自动判定"); oy = oy + S.rowH
        toggle("rangePick",   rx, by + oy, rowW, "范围内拾取");     oy = oy + S.rowH
        txt(rx, by + oy + 6, "范围半径", T.text, 12)
        txt(rx + rowW - 40, by + oy + 6, "10.0", T.accent, 13)
        oy = oy + 28
        glassChip(rx, by + oy, rx + rowW, by + oy + 7, 4, true)
        oy = oy + 22
        txt(rx, by + oy, "半径已锁定，不可调整", T.dim, 11)

    elseif S.tab == 3 then
        toggle("safeMode", rx, by + oy, rowW, "限帧模式（省电）"); oy = oy + S.rowH
        txt(rx, by + oy + 4, "UI 缩放  " .. string.format("%.2f", CFG.fontScale), T.text, 12)
        oy = oy + 26
        glassChip(rx, by + oy, rx + rowW * ((CFG.fontScale - 0.8) / 0.6), by + oy + 5, 3, true)
        oy = oy + 26
        txt(rx, by + oy + 4, "范围像素换算", T.text, 12)
        txt(rx + rowW - 40, by + oy + 4, string.format("%.1f", CFG.pxPerUnit or 9.5), T.dim, 12)
        oy = oy + 24
        local p = ((CFG.pxPerUnit or 9.5) - 4) / 12
        glassChip(rx, by + oy, rx + rowW, by + oy + 5, 3, false)
        if p > 0.01 then glassChip(rx, by + oy, rx + rowW * p, by + oy + 5, 3, true) end
        oy = oy + 28

        glassChip(rx, by + oy, rx + (rowW - 10) / 2, by + oy + 26, 8, false)
        txt(rx + 16, by + oy + 8, "保存配置", T.dim, 11)
        glassChip(rx + (rowW + 10) / 2, by + oy, rx + rowW, by + oy + 26, 8, false)
        txt(rx + (rowW + 10) / 2 + 16, by + oy + 8, "恢复默认", T.dim, 11)

        oy = oy + 36
        txt(rx, by + oy, "音量上：隐藏   音量下：显示", T.dim, 11)
    end
end

-- ==================== 可见性控制 ====================
local function setPanel(v)
    S.visible = v and true or false
    if S.visible then S.open = true end
end

-- ==================== 音量键：监听通道 ====================
local function handleVolumeCode(code)
    if code == VK_UP then setPanel(false); return true end
    if code == VK_DOWN then setPanel(true); return true end
    return false
end

local function installKeyHook()
    if not KeyNS then return false end
    local reg = KeyNS.setListener or KeyNS.listen or KeyNS.setKeyListener
    if type(reg) ~= "function" then return false end
    local ok = pcall(reg, function(code, action)
        -- 部分引擎 action: 0/1 或 "down"/"up"；只响应按下
        if action == 1 or action == "up" or action == "UP" then return end
        handleVolumeCode(code)
    end)
    return ok
end

-- ==================== 音量键：轮询通道 ====================
local prevUp, prevDown = false, false

local function pollVolume()
    if not KeyNS then return end
    local f = KeyNS.isPressed or KeyNS.check or KeyNS.isDown
    if type(f) ~= "function" then return end
    local u = false
    local d = false
    local ok1, r1 = pcall(f, VK_UP)
    if ok1 then u = r1 and true or false end
    local ok2, r2 = pcall(f, VK_DOWN)
    if ok2 then d = r2 and true or false end
    if u and not prevUp then setPanel(false) end
    if d and not prevDown then setPanel(true) end
    prevUp, prevDown = u, d
end

-- ==================== 进游戏自动弹出 ====================
local GAME_PKGS = {
    "com.tencent.tmgp.pubgmhd",   -- 国服
    "com.tencent.ig",             -- 国际服
    "com.rekoo.pubgm",
    "com.pubg.krmobile",
    "com.vng.pubgmobile",
}

local function gameForeground()
    if type(isProcessRunning) == "function" then
        for _, pkg in ipairs(GAME_PKGS) do
            local ok, r = pcall(isProcessRunning, pkg)
            if ok and r then return true end
        end
        return false
    end
    if type(getForegroundPackage) == "function" then
        local ok, p = pcall(getForegroundPackage)
        if ok and type(p) == "string" then
            local pl = p:lower()
            for _, pkg in ipairs(GAME_PKGS) do
                if pl:find(pkg, 1, true) then return true end
            end
            return false
        end
    end
    -- 无法检测进程时，视为已进入，直接弹出
    return true
end

local function autoPopup()
    if S.autoPopped then return end
    if gameForeground() then
        S.autoPopped = true
        setPanel(true)
    end
end

-- ==================== 触控 ====================
local lastDown = { x = 0, y = 0, t = 0 }

local function onTap(mx, my)
    if not S.visible or not S.open then return end
    if not hit(mx, my, S.x, S.y + 34, S.w, S.h - 34) then return end
    local ly = my - (S.y + 34)
    local lx = mx - S.x
    local rowW = S.w - 28
    local rx = 14

    if S.tab == 1 then
        local sy = 86 - 34 + 5 * S.rowH
        if hit(mx, my, S.x + rx, S.y + 34 + sy, rowW, 30) then
            local p = math.max(0, math.min(1, (lx - rx) / rowW))
            CFG.maxDist = math.floor(50 + p * 950)
        end
    elseif S.tab == 3 then
        -- 换算滑块
        local sy = (86 - 34) + S.rowH + 26 + 26 + 24
        if hit(mx, my, S.x + rx, S.y + 34 + sy, rowW, 20) then
            local p = math.max(0, math.min(1, (lx - rx) / rowW))
            CFG.pxPerUnit = 4 + p * 12
        end
        -- 保存按钮
        local by = (86 - 34) + S.rowH + 26 + 26 + 24 + 28
        if hit(mx, my, S.x + rx, S.y + 34 + by, (rowW - 10) / 2, 26) then
            saveCfg()
        end
    end
end

local function onTouch(id, action, mx, my)
    if action == Touch.DOWN then
        lastDown.x, lastDown.y, lastDown.t = mx, my, os.time()
        if S.visible and hit(mx, my, S.x, S.y, S.w, 34) then
            S.dragging = true
            S.dragX, S.dragY = mx - S.x, my - S.y
            return
        end
        if S.visible and S.open and hit(mx, my, S.x + S.w - 32, S.y + 8, 24, 22) then
            S.open = false
            return
        end
        if S.visible and not S.open and hit(mx, my, S.x, S.y, 100, 34) then
            S.open = true
            return
        end
        if S.visible and S.open and hit(mx, my, S.x, S.y + 34, S.w, S.h - 34) then
            local ly = my - (S.y + 34)
            local lx = mx - S.x
            local tw = S.w / #TABS
            if ly < 36 then
                local t = math.floor(lx / tw) + 1
                if t >= 1 and t <= #TABS then S.tab = t end
                return
            end
            local by = 86
            local rly = ly - by + 34
            local i = math.floor(rly / S.rowH) + 1
            local oy = (i - 1) * S.rowH
            if S.tab == 1 then
                local keys = {"espLine","espBox","espName","espDist","espHealth"}
                local k = keys[i]
                if k and hit(mx, my, S.x + 14, S.y + by + oy, S.w - 28, S.rowH) then
                    CFG[k] = not CFG[k]; return
                end
            elseif S.tab == 2 then
                local keys = {"rangeOn","rangeCircle","rangeAttack","rangePick"}
                local k = keys[i]
                if k and hit(mx, my, S.x + 14, S.y + by + oy, S.w - 28, S.rowH) then
                    CFG[k] = not CFG[k]; return
                end
            elseif S.tab == 3 then
                if i == 1 and hit(mx, my, S.x + 14, S.y + by + oy, S.w - 28, S.rowH) then
                    CFG.safeMode = not CFG.safeMode; return
                end
            end
        end
    elseif action == Touch.MOVE then
        if S.dragging then
            S.x = math.max(0, math.min(D.width() - S.w, mx - S.dragX))
            S.y = math.max(0, math.min(D.height() - 34, my - S.dragY))
        end
    elseif action == Touch.UP then
        S.dragging = false
        if math.abs(mx - lastDown.x) < 8 and math.abs(my - lastDown.y) < 8
           and os.time() - lastDown.t < 1 then
            onTap(mx, my)
        end
    end
end

-- ==================== 主循环 ====================
loadCfg()
local hooked = installKeyHook()

local function frame()
    S.tick = S.tick + 1

    for k, v in pairs(CFG) do
        if type(v) == "boolean" then
            local cur = S.anim[k] or (v and 1 or 0)
            local target = v and 1 or 0
            S.anim[k] = cur + (target - cur) * 0.25
        end
    end

    renderESP()
    renderRange()

    if S.visible then
        renderPanel()
    else
        -- 隐藏态：左上角留一个玻璃小标签，提示当前是隐藏状态
        glassPanel(12, 12, 74, 40, 10)
        txt(22, 22, "隐藏中", T.dim, 10)
    end
end

local function main()
    if Touch then Touch.setListener(onTouch) end
    while true do
        autoPopup()
        pollVolume()
        frame()
        sleep(CFG.safeMode and 33 or 16)
    end
end

main()