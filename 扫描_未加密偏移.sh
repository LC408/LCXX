#!/system/bin/sh

echo "======================================="
echo "和平精英偏移查找 v2.0"
echo "======================================="
echo ""

# 检查ROOT
if [ "$(id -u)" != "0" ]; then
    echo "[-] 需要ROOT权限"
    exit 1
fi

# 等待游戏启动
echo "[*] 等待游戏进程..."
for i in 1 2 3 4 5; do
    PID=$(pgrep -f com.tencent.tmgp.pubgmhd | head -1)
    if [ -n "$PID" ]; then
        break
    fi
    echo "    尝试 $i/5..."
    sleep 2
done

if [ -z "$PID" ]; then
    echo "[-] 游戏未运行！"
    echo ""
    echo "请按以下步骤操作："
    echo "1. 启动和平精英"
    echo "2. 进入训练场"
    echo "3. 确保有其他玩家"
    echo "4. 保持游戏在前台"
    echo "5. 重新运行本脚本"
    exit 1
fi

echo "[+] 找到游戏PID: $PID"

# 检查进程是否还存在
if [ ! -d "/proc/$PID" ]; then
    echo "[-] 进程已退出"
    exit 1
fi

echo ""

# 使用GameGuardian的内存搜索方法
echo "[*] 使用GG搜索引擎..."

# 创建临时目录
WORK_DIR="/data/local/tmp/offset_finder_$$"
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# 生成GG Lua脚本
cat > search.lua <<'EOLUA'
-- 自动偏移查找
gg.clearResults()
gg.setRanges(gg.REGION_C_ALLOC | gg.REGION_ANONYMOUS)

-- 搜索身高 480.0
gg.searchNumber("480.0", gg.TYPE_FLOAT)
local count = gg.getResultsCount()

if count == 0 then
    print("[-] 未找到身高特征")
    os.exit(1)
end

print("[+] 找到 " .. count .. " 个候选")

-- 获取所有结果
local results = gg.getResults(math.min(count, 100))

-- 测试偏移
local found = {}
local ROOT_OFFSETS = {0x260, 0x268, 0x270}
local LOC_OFFSETS = {0x200, 0x210, 0x220}
local HEIGHT_OFFSETS = {0x38C4, 0x38C8, 0x38D0}

for _, result in ipairs(results) do
    local heightAddr = result.address
    
    for _, heightOff in ipairs(HEIGHT_OFFSETS) do
        local objAddr = heightAddr - heightOff
        
        -- 跳过无效地址
        if objAddr > 0x700000000000 and objAddr < 0x800000000000 then
            for _, rootOff in ipairs(ROOT_OFFSETS) do
                -- 读取RootComponent指针
                local rootComp = gg.getValues({{address = objAddr + rootOff, flags = gg.TYPE_QWORD}})[1].value
                
                if rootComp > 65536 and rootComp < 0x7FFFFFFFFF then
                    for _, locOff in ipairs(LOC_OFFSETS) do
                        -- 读取坐标
                        local coords = gg.getValues({
                            {address = rootComp + locOff, flags = gg.TYPE_FLOAT},
                            {address = rootComp + locOff + 4, flags = gg.TYPE_FLOAT},
                            {address = rootComp + locOff + 8, flags = gg.TYPE_FLOAT}
                        })
                        
                        local x = coords[1].value
                        local y = coords[2].value
                        local z = coords[3].value
                        
                        -- 验证坐标有效
                        if x ~= 0 and y ~= 0 and z ~= 0 and
                           x == x and y == y and z == z then  -- 检查不是NaN
                            
                            local key = string.format("%X_%X_%X", rootOff, locOff, heightOff)
                            found[key] = (found[key] or 0) + 1
                            
                            print(string.format("[✓] 对象: 0x%X, Root: 0x%X, Loc: 0x%X, H: 0x%X",
                                objAddr, rootOff, locOff, heightOff))
                            print(string.format("    坐标: %.2f, %.2f, %.2f", x, y, z))
                        end
                    end
                end
            end
        end
    end
end

-- 找出最常见的组合
local bestKey = nil
local bestCount = 0
for key, cnt in pairs(found) do
    if cnt > bestCount then
        bestKey = key
        bestCount = cnt
    end
end

if bestKey then
    local r, l, h = bestKey:match("(%x+)_(%x+)_(%x+)")
    print("")
    print("=======================================")
    print("[√] 最终偏移 (出现" .. bestCount .. "次):")
    print("")
    print("#define ROOT_COMPONENT 0x" .. r)
    print("#define RELATIVE_LOCATION 0x" .. l)
    print("#define HEIGHT_FILTER 0x" .. h)
    print("=======================================")
    
    -- 保存结果
    local f = io.open("/data/local/tmp/eta/pubgmhd_analysis/found_offsets.h", "w")
    f:write("// 和平精英偏移 (v2.0自动生成)\n")
    f:write(string.format("#define ROOT_COMPONENT 0x%s\n", r))
    f:write(string.format("#define RELATIVE_LOCATION 0x%s\n", l))
    f:write(string.format("#define HEIGHT_FILTER 0x%s\n", h))
    f:close()
    
    print("")
    print("结果已保存到: found_offsets.h")
else
    print("[-] 未找到有效偏移")
    os.exit(1)
end
EOLUA

echo "[*] 启动GG..."

# 检查GG是否安装
if [ ! -f /data/data/catch_.the_.fire_/files/lua ]; then
    echo "[-] GameGuardian未安装"
    echo ""
    echo "方案B：使用纯内存扫描"
    echo "======================================"
    
    # 纯内存扫描方案
    echo "[*] 直接读取内存..."
    
    # 获取匿名内存区域
    cat /proc/$PID/maps | grep -E '^[0-9a-f]+-[0-9a-f]+ rw' | \
        grep -v '/dev\|/system\|/apex\|linker\|stack' | \
        awk '{print $1}' > mem_regions.txt
    
    echo "[+] 找到 $(wc -l < mem_regions.txt) 个内存区域"
    
    # 使用更高效的搜索方法
    echo "[*] 搜索身高特征 0x43F00000..."
    
    > found_addrs.txt
    count=0
    
    while read region && [ $count -lt 100 ]; do
        start=$(echo "$region" | cut -d'-' -f1)
        end=$(echo "$region" | cut -d'-' -f2)
        
        start_dec=$((0x$start))
        end_dec=$((0x$end))
        size=$((end_dec - start_dec))
        
        # 跳过太小或太大的区域
        if [ $size -lt 4096 ] || [ $size -gt $((100 * 1024 * 1024)) ]; then
            continue
        fi
        
        echo "    扫描 0x$start ($((size / 1024))KB)..."
        
        # 使用strings + grep方式
        dd if=/proc/$PID/mem bs=$size skip=0 iflag=skip_bytes count=1 2>/dev/null | \
            xxd -g4 -e | grep -E '43f00000|0000f043' | while read line; do
            
            offset=$(echo "$line" | awk '{print $1}' | tr -d ':')
            addr=$((start_dec + 0x$offset))
            printf "0x%X\n" $addr >> found_addrs.txt
            count=$((count + 1))
            
            if [ $count -ge 100 ]; then
                break
            fi
        done
        
    done < mem_regions.txt
    
    result_count=$(wc -l < found_addrs.txt 2>/dev/null || echo 0)
    echo "[+] 找到 $result_count 个候选地址"
    
    if [ $result_count -eq 0 ]; then
        echo ""
        echo "[-] 未找到结果"
        echo ""
        echo "建议："
        echo "1. 确保在训练场且能看到其他玩家"
        echo "2. 安装GameGuardian获得更好效果"
        echo "3. 或使用IDA Pro静态分析"
        cd /
        rm -rf "$WORK_DIR"
        exit 1
    fi
    
    echo ""
    echo "[*] 测试偏移..."
    echo "（这需要1-2分钟）"
    
    # 这里需要实现内存读取验证
    # 由于Shell限制，建议使用GG或编译的工具
    
    echo ""
    echo "[!] 找到候选地址但无法完整验证"
    echo "    候选数量: $result_count"
    echo "    建议安装GameGuardian完成自动验证"
    echo ""
    echo "候选地址已保存到: $WORK_DIR/found_addrs.txt"
    
else
    # 使用GG执行
    echo "[+] GameGuardian已安装"
    /data/data/catch_.the_.fire_/files/lua search.lua
fi

# 清理
cd /
rm -rf "$WORK_DIR"

echo ""
echo "======================================="
echo "完成！"
