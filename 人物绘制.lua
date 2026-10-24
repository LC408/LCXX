screenW = 1264          -- 原为 screenH，绘制里用的是 screenW，这里补上
screenH = 1264
人物高度 = 2.1
脚底偏移 = 1.4
头顶偏移 = 0.8
左右偏移 = 0
上下偏移 = 0
显示调试 = false
x64 = true
ShuZuShuJu = { false, 0, 0xC, 0x10, 0x14, false, {} }

JieGouTi = {
    false, {}, 0xEC, 0xF0, 0xF4,   -- 坐标偏移
    false, 0, 0, 4, 100            -- 血量关闭
}

HuiZhiShuJu = nil
local matrixValues = {}
local HuanCun = {}

local paintBox = newPaint()
paintBox:setColor("#FF00FF00")
paintBox:setWidth(2)
paintBox:setStyle(0)

local paintLine = newPaint()
paintLine:setColor("#FFFF0000")
paintLine:setWidth(2)
paintLine:setStyle(0)

local paintDist = newPaint()
paintDist:setColor("#FFFFFFFF")   -- 原为 paintDistsetColor
paintDist:setStyle(1)
paintDist:setTextSize(16)

local view = newView()
view:show(function(canvas)
    canvas:save()
    local data = HuiZhiShuJu
    if data then
        for _, v in ipairs(data) do
            local left, top, w, h, dist = v[1], v[2], v[3], v[4], v[5]  -- 原为 v[11
            if left + w > 0 and left < screenW and top + h > 0 and top < screenH then
                canvas:drawRect({ left, top, left + w, top + h }, paintBox)
                canvas:drawLine(screenW / 2, 50, left + w / 2, top, paintLine)  -- 原为 top. 
                canvas:drawText(string.format("距离：%.1f", dist), left, top - 10, paintDist)
            end
        end
    end
    canvas:restore()
end, 30)

gg.showUiButton()

pcall(function()
    while true do
        if gg.isClickedUiButton() then
            local alert = gg.prompt(
                { "人物高度", "脚底偏移", "头顶偏移" },
                { 人物高度, 脚底偏移, 头顶偏移 },
                { "number", "number", "number" }
            )
            if alert then
                人物高度, 脚底偏移, 头顶偏移 = alert[1], alert[2], alert[3]
            end
        end

        -- 读取矩阵
        local matrixAddrs = {}
        for i = 0, 15 do
            matrixAddrs[#matrixAddrs + 1] = { address = JuZhen + i * 4, flags = 16 }
        end
        matrixAddrs[#matrixAddrs + 1] = { address = ShuZu + ShuZuShuJu[3], flags = 4 }
        local matrixVals = gg.getValues(matrixAddrs)
        local m = {}
        for i, v in ipairs(matrixVals) do m[i] = v.value end

        HuanCun = {}   -- 每帧清空缓存，否则会无限增长

        for i = 0, objCount - 1 do
            local objAddr = ShuZu + ShuZuShuJu[4] + (ShuZuShuJu[5] * i)
            local objVal = gg.getValues({ { address = objAddr, flags = x64 and 32 or 4 } })[1].value
            if ShuZuShuJu[1] and objVal == ShuZuShuJu[2] then break end

            local baseAddr = x64 and objVal or (objVal & 0xFFFFFFFF)
            if baseAddr == 0 then goto continue_obj end

            local coordAddr = baseAddr
            if JieGouTi[1] then
                for _, off in ipairs(JieGouTi[2]) do
                    local res = gg.getValues({ { address = coordAddr + off, flags = x64 and 32 or 4 } })
                    coordAddr = res[1].value
                    if not x64 then coordAddr = coordAddr & 0xFFFFFFFF end
                end
            end

            local values = gg.getValues({
                { address = coordAddr + JieGouTi[3], flags = 16 },
                { address = coordAddr + JieGouTi[4], flags = 16 },
                { address = coordAddr + JieGouTi[5], flags = 16 }
            })
            local x = values[1].value
            local z = values[2].value
            local y = values[3].value

            local clipXCenter = m[1] * x + m[5] * z + m[9] * y + m[13]
            local clipW = m[4] * x + m[8] * z + m[12] * y + m[16]
            if clipW <= 0 then goto continue_obj end

            local ndcXCenter = clipXCenter / clipW
            local screenXCenter = screenW / 2 + ndcXCenter * screenW / 2

            local distance = clipW
            local scale = math.min(1, 50 / distance)
            local dynamicFootOffset = 脚底偏移 * scale
            local dynamicHeadOffset = 头顶偏移 * scale

            local clipYFoot = m[2] * x + m[6] * (z - dynamicFootOffset) + m[10] * y + m[14]
            local ndcYFoot = clipYFoot / clipW
            local screenYFoot = screenH / 2 - ndcYFoot * screenH / 2

            local clipYHead = m[2] * x + m[6] * (z + dynamicHeadOffset) + m[10] * y + m[14]
            local ndcYHead = clipYHead / clipW
            local screenYHead = screenH / 2 - ndcYHead * screenH / 2

            local boxHeight = math.abs(screenYFoot - screenYHead)
            boxHeight = math.max(boxHeight, 10)
            local boxWidth = boxHeight / 1.8

            local left = screenXCenter - boxWidth / 2 + 左右偏移
            local top = screenYHead + 上下偏移

            HuanCun[#HuanCun + 1] = { left, top, boxWidth, boxHeight, distance }
            ::continue_obj::
        end

        HuiZhiShuJu = HuanCun
        gg.sleep(10)
    end
end)

while true do gg.sleep(10000) end