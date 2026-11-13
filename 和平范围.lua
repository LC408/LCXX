--[[
============================================================
  PUBG Mobile · 绿色射线透视 + 10.0 范围  单文件版
  环境：安卓 Lua 脚本引擎（蜂窝 / 触动 / GG 衍生）
  用法：整文件丢进脚本目录，直接加载运行
  绿色线条已硬编码，范围半径锁定 10.0
============================================================
]]

-- ==================== 引擎 API 别名 ====================
-- 不同引擎只改这一段
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
local CFG_FILE = "pubg_green_esp.cfg"

-- ==================== 硬编码主题 ====================
-- 透视线条：绿色，不可通过 UI 改
local GREEN_LINE = { 0.10, 1.00, 0.35, 1.00 }   -- 亮绿射线
local GREEN_BOX  = { 0.10, 1.00, 0.35, 0.85 }   -- 同色方框
local GREEN_FILL = { 0.10, 1.00, 0.35, 0.12 }   -- 范围圆淡填充

local T = {
    bg     = {0.05, 0.06, 0.08, 0.93},
    header = {0.09, 0.11, 0.14, 0.97},
    accent = {0.10, 1.00, 0.35, 1.00},
    text   = {0.92, 0.94, 0.96, 1.00},
    dim    = {0.55, 0.58, 0.62, 1.00},
    track  = {0.17, 0.19, 0.23, 1.00},
    line   = {0.22, 0.25, 0.30, 1.00},
}

-- ==================== 固定参数 ====================
local RANGE_RADIUS = 10.0        -- 范围半径，锁死 10.0

-- ==================== 运行状态 ====================
local S = {
    x = 30, y = 110,
    w = 290, h = 380,
    open = true,
    tab = 1,                     -- 1 透视  2 范围  3 设置
    dragging = false,
    dragX = 0, dragY = 0,
    rowH = 34,
    anim = {},
}

local TABS = { "透视", "范围", "设置" }

-- ==================== 功能开关 ====================
local CFG = {
    -- 透视
    espLine   = true,            -- 绿色射线
    espBox    = true,            -- 绿色方框
    espName   = true,
    espDist   = true,
    espHealth = true,
    maxDist   = 300,
    linePos   = 1,               -- 1 底部  2 中心  3 顶部
    -- 范围
    rangeOn      = true,         -- 范围功能总开关
    rangeAttack  = true,         -- 范围内自动判定
    rangePick    = false,        -- 范围拾取
    rangeCircle  = true,         -- 屏幕画范围圈
    rangeCenterX = 0,            -- 范围圆心：屏幕坐标，0 = 自动取自身
    rangeCenterY = 0,
    -- 设置
    safeMode  = true,
    fontScale = 1.0,
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

local function box(x, y, w, h, c, round)
    setC(c)
    if round then D.roundRect(x, y, x + w, y + h, 6)
    else D.rect(x, y, x + w, y + h) end
end

-- ==================== 控件 ====================
local function toggle(key, x, y, w, label)
    local prog = S.anim[key] or (CFG[key] and 1 or 0)
    txt(x, y + 8, label, T.text, 12)
    local tx, ty, tw, th = x + w - 44, y + 9, 38, 18
    local on, off = T.track, T.accent
    setC({
        off[1] + (on[1] - off[1]) * (1 - prog),
        off[2] + (on[2] - off[2]) * (1 - prog),
        off[3] + (on[3] - off[3]) * (1 - prog),
        1,
    })
    D.roundRect(tx, ty, tx + tw, ty + th, th / 2)
    setC({0.95, 0.97, 0.99, 1})
    local kx = tx + 2 + (tw - th) * prog
    D.circle(kx + th / 2 - 2, ty + th / 2, th / 2 - 2)
end

local function slider(key, x, y, w, label, min, max)
    local v = CFG[key]
    local p = (v - min) / (max - min)
    txt(x, y + 4, label, T.text, 12)
    txt(x + w - 46, y + 4, tostring(math.floor(v)), T.dim, 12)
    local tx, ty, tw, th = x, y + 20, w, 4
    box(tx, ty, tw, th, T.track, true)
    box(tx, ty, tw * p, th, T.accent, true)
    setC({1,1,1,1})
    D.circle(tx + tw * p, ty + th / 2, 6)
end

local function seg(key, x, y, w, label, opts)
    txt(x, y + 4, label, T.text, 12)
    local n = #opts
    local bw = (w - 8) / n
    for i, o in ipairs(opts) do
        local bx = x + (i - 1) * bw
        local sel = CFG[key] == i
        box(bx, y + 18, bw - 4, 20, sel and T.accent or T.track, true)
        txt(bx + 6, y + 22, o, sel and {0.05,0.05,0.05,1} or T.dim, 11)
    end
end

-- ==================== 命中检测 ====================
local function hit(mx, my, x, y, w, h)
    return mx >= x and mx <= x + w and my >= y and my <= y + h
end

-- ==================== 数据接口 ====================
-- getEntities() 必须返回：
-- { {sx=屏幕x, sy=屏幕y, sw=框宽, sh=框高, hp=0-100, dist=米,
--    wx=世界x, wy=世界y, name="昵称"}, ... }
-- 数据链路：进程内存 → 人物世界坐标 → 视角矩阵投影 → 屏幕坐标
-- 该函数由外部内存模块提供；UI 层只消费其输出
if not getEntities then
    function getEntities() return {} end
end

-- getSelfWorld() 必须返回 { wx=自身世界x, wy=自身世界y, sx=自身屏幕x, sy=自身屏幕y }
-- 用于范围判定与屏幕范围圈锚点
if not getSelfWorld then
    function getSelfWorld() return nil end
end

-- ==================== 透视渲染（绿色硬编码） ====================
local function renderESP()
    local list = getEntities()
    if not list then return end
    local sw, sh = D.width(), D.height()

    for _, e in ipairs(list) do
        if e.dist <= CFG.maxDist then
            -- 绿色方框
            if CFG.espBox then
                setC(GREEN_BOX)
                D.rect(e.sx, e.sy, e.sx + e.sw, e.sy + e.sh)
            end

            -- 绿色射线（从屏幕边缘指向敌人脚底）
            if CFG.espLine then
                local fromX, fromY = sw / 2, sh
                if CFG.linePos == 2 then fromY = sh / 2
                elseif CFG.linePos == 3 then fromY = 0 end
                setC(GREEN_LINE)
                D.line(fromX, fromY, e.sx + e.sw / 2, e.sy + e.sh)
            end

            -- 血量条
            if CFG.espHealth then
                local hpc = {0.9, 0.2, 0.2, 1}
                if e.hp > 50 then hpc = {0.9, 0.75, 0.15, 1} end
                if e.hp > 75 then hpc = {0.2, 0.85, 0.35, 1} end
                box(e.sx - 6, e.sy, 3, e.sh, T.track, false)
                box(e.sx - 6, e.sy + e.sh * (1 - e.hp / 100), 3, e.sh * e.hp / 100, hpc, false)
            end

            -- 昵称
            if CFG.espName and e.name then
                txt(e.sx, e.sy - 16, e.name, GREEN_LINE, 11)
            end

            -- 距离
            if CFG.espDist then
                txt(e.sx, e.sy + e.sh + 2,
                    tostring(math.floor(e.dist)) .. "m",
                    GREEN_LINE, 11)
            end
        end
    end
end

-- ==================== 范围功能（半径锁死 10.0） ====================
local function inRange(a, b, r)
    local dx = a.wx - b.wx
    local dy = a.wy - b.wy
    return (dx * dx + dy * dy) <= (r * r)
end

local function renderRange()
    if not CFG.rangeOn then return end

    local self = getSelfWorld()
    if not self then return end

    -- 屏幕范围圈：以自身屏幕坐标为中心，按世界 10.0 半径投影出屏幕半径
    if CFG.rangeCircle then
        -- 屏幕像素半径：需要世界单位到像素的换算系数 PX_PER_UNIT
        -- 这里按典型第三人称视角给出经验值，可在设置页调
        local PX_PER_UNIT = CFG.pxPerUnit or 9.5
        local rpx = RANGE_RADIUS * PX_PER_UNIT
        setC(GREEN_FILL)
        D.circle(self.sx, self.sy, rpx)
        setC(GREEN_LINE)
        D.circle(self.sx, self.sy, rpx)
    end

    -- 范围内目标判定
    local list = getEntities()
    if not list then return end

    for _, e in ipairs(list) do
        if e.wx and e.wy and inRange(self, e, RANGE_RADIUS) then
            -- 范围内目标：绿色加粗标记
            setC(GREEN_LINE)
            D.rect(e.sx - 2, e.sy - 2, e.sx + e.sw + 2, e.sy + e.sh + 2)

            -- 自动攻击判定（是否真的执行由内存模块接）
            if CFG.rangeAttack and onRangeTarget then
                onRangeTarget(e)
            end

            -- 范围拾取
            if CFG.rangePick and onRangePick then
                onRangePick(e)
            end
        end
    end
end

-- ==================== 面板渲染 ====================
local function renderPanel()
    local x, y, w = S.x, S.y, S.w
    if not S.open then
        box(x, y, 92, 34, T.header, true)
        txt(x + 12, y + 10, "ESP ▶", T.accent, 12)
        return
    end

    box(x, y, w, S.h, T.bg, true)
    box(x, y, w, 34, T.header, true)
    txt(x + 14, y + 9, "PUBG · 绿透 + 10.0范围", T.text, 12)
    txt(x + w - 30, y + 9, "—", T.dim, 14)

    -- 标签页
    local tw = w / #TABS
    for i, name in ipairs(TABS) do
        local tx = x + (i - 1) * tw
        local sel = (S.tab == i)
        if sel then box(tx + 6, y + 40, tw - 12, 24, T.accent, true) end
        txt(tx + (tw - #name * 12) / 2, y + 46, name,
            sel and {0.05,0.05,0.05,1} or T.dim, 12)
    end
    box(x + 8, y + 70, w - 16, 1, T.line)

    local by = y + 78
    local rowW = w - 24
    local rx = x + 12
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
        oy = oy + 40
        seg("linePos", rx, by + oy, rowW, "射线起点", {"底部","中心","顶部"})

    elseif S.tab == 2 then
        toggle("rangeOn",     rx, by + oy, rowW, "范围功能总开关"); oy = oy + S.rowH
        toggle("rangeCircle", rx, by + oy, rowW, "屏幕范围圈");     oy = oy + S.rowH
        toggle("rangeAttack", rx, by + oy, rowW, "范围内自动判定"); oy = oy + S.rowH
        toggle("rangePick",   rx, by + oy, rowW, "范围内拾取");     oy = oy + S.rowH
        -- 半径固定 10.0，只展示不可改
        txt(rx, by + oy + 6, "范围半径", T.text, 12)
        txt(rx + rowW - 40, by + oy + 6, "10.0", T.accent, 13)
        oy = oy + 30
        box(rx, by + oy, rowW, 6, T.accent, true)
        oy = oy + 20
        txt(rx, by + oy, "半径已锁定，不可调整", T.dim, 11)

    elseif S.tab == 3 then
        toggle("safeMode", rx, by + oy, rowW, "限帧模式（省电）"); oy = oy + S.rowH
        txt(rx, by + oy + 4, "UI 缩放  " .. string.format("%.2f", CFG.fontScale), T.text, 12)
        oy = oy + 26
        box(rx, by + oy, rowW * ((CFG.fontScale - 0.8) / 0.6), 4, T.accent, true)
        oy = oy + 30
        txt(rx, by + oy + 4, "范围像素换算", T.text, 12)
        txt(rx + rowW - 40, by + oy + 4,
            string.format("%.1f", CFG.pxPerUnit or 9.5), T.dim, 12)
        oy = oy + 24
        local p = ((CFG.pxPerUnit or 9.5) - 4) / 12
        box(rx, by + oy, rowW, 4, T.track, true)
        box(rx, by + oy, rowW * p, 4, T.accent, true)
        oy = oy + 30
        box(rx, by + oy + 4, (rowW - 8) / 2, 24, T.track, true)
        txt(rx + 14, by + oy + 8, "保存配置", T.dim, 11)
        box(rx + (rowW + 8) / 2, by + oy + 4, (rowW - 8) / 2, 24, T.track, true)
        txt(rx + (rowW + 8) / 2 + 14, by + oy + 8, "恢复默认", T.dim, 11)
    end
end

-- ==================== 触控 ====================
local lastDown = { x = 0, y = 0, t = 0 }

local function onTap(mx, my)
    if not S.open then return end
    if not hit(mx, my, S.x, S.y + 34, S.w, S.h - 34) then return end
    local ly = my - (S.y + 34)
    local lx = mx - S.x
    local rowW = S.w - 24
    local rx = 12

    if S.tab == 1 then
        local sy = 40 + 5 * S.rowH
        if hit(mx, my, S.x + rx, S.y + 34 + sy, rowW, 30) then
            local p = math.max(0, math.min(1, (lx - rx) / rowW))
            CFG.maxDist = math.floor(50 + p * 950)
        end
    elseif S.tab == 3 then
        local sy = 40 + S.rowH
        local base = S.y + 34 + 78 + sy + 20
        if hit(mx, my, S.x + rx, base, rowW, 20) then
            local p = math.max(0, math.min(1, (lx - rx) / rowW))
            CFG.pxPerUnit = 4 + p * 12
        end
        -- 保存按钮
        local by = 78 + 40 + S.rowH + 26 + 30 + 24 + 30
        if hit(mx, my, S.x + rx, S.y + 34 + by + 4, (rowW - 8) / 2, 24) then
            saveCfg()
            txt(S.x + 20, S.y + S.h - 24, "已保存", T.accent, 11)
        end
    end
end

local function onTouch(id, action, mx, my)
    if action == Touch.DOWN then
        lastDown.x, lastDown.y, lastDown.t = mx, my, os.time()
        if S.open and hit(mx, my, S.x, S.y, S.w, 34) then
            S.dragging = true
            S.dragX, S.dragY = mx - S.x, my - S.y
            return
        end
        if S.open and hit(mx, my, S.x + S.w - 32, S.y + 6, 24, 22) then
            S.open = false
            return
        end
        if not S.open and hit(mx, my, S.x, S.y, 92, 34) then
            S.open = true
            return
        end
        if S.open and hit(mx, my, S.x, S.y + 34, S.w, S.h - 34) then
            local ly = my - (S.y + 34)
            local lx = mx - S.x
            local tw = S.w / #TABS
            if ly < 30 then
                local t = math.floor(lx / tw) + 1
                if t >= 1 and t <= #TABS then S.tab = t end
                return
            end
            -- 开关点击
            local by = 78
            local rly = ly - by
            local i = math.floor(rly / S.rowH) + 1
            local oy = (i - 1) * S.rowH
            if S.tab == 1 then
                local keys = {"espLine","espBox","espName","espDist","espHealth"}
                local k = keys[i]
                if k and hit(mx, my, S.x + 12, S.y + 34 + by + oy, S.w - 24, S.rowH) then
                    CFG[k] = not CFG[k]; return
                end
            elseif S.tab == 2 then
                local keys = {"rangeOn","rangeCircle","rangeAttack","rangePick"}
                local k = keys[i]
                if k and hit(mx, my, S.x + 12, S.y + 34 + by + oy, S.w - 24, S.rowH) then
                    CFG[k] = not CFG[k]; return
                end
            elseif S.tab == 3 then
                local keys = {"safeMode"}
                local k = keys[i]
                if k and hit(mx, my, S.x + 12, S.y + 34 + by + oy, S.w - 24, S.rowH) then
                    CFG[k] = not CFG[k]; return
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

local function frame()
    for k, v in pairs(CFG) do
        if type(v) == "boolean" then
            local cur = S.anim[k] or (v and 1 or 0)
            local target = v and 1 or 0
            S.anim[k] = cur + (target - cur) * 0.25
        end
    end
    renderESP()
    renderRange()
    renderPanel()
end

local function main()
    if Touch then Touch.setListener(onTouch) end
    while true do
        frame()
        sleep(CFG.safeMode and 33 or 16)
    end
end

main()