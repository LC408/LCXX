// 骨骼绘制.h — ESP「骨骼样式」绘制（单编译单元模块头：绘图.h 在对象循环里直接调用；不要单独编译）
//
// 【样式绘制例子】以后加新的绘制样式（方框样式、人形轮廓、特效圈…），照这套模式写：
//   ①调用方把「世界坐标」投影成「屏幕坐标」（WTS），给出 sv[] 与可用性 okB[] —— 本文件不碰矩阵/相机；
//   ②本文件只做「屏幕坐标 → 形状」：线段 / 圆 / 普通椭圆 / 带旋转的椭圆 / 实心点；
//   ③掩体变色统一走骨可见表（boneVis[]）；每个形状逐点判可用性，缺哪根骨骼只跳过那一块；
//   ④想加部件：在「体型轮廓」段后面照抄，用 sv[] 里现成的关节算位置。
//
#pragma once

// 绘制一整具骨骼样式：骨骼线 + 头部圈 + 体型轮廓（胸 / 下部）
// sv/okB：15 根骨骼的屏幕坐标与可用性（调用方投影好）；okN：可用数量；
// 骨可见：掩体可见性表；isBot：人机（决定掩体颜色）；boxH：对象框高（头圈半径）
static void 绘制骨骼样式(ImDrawList *Draw, const Vector2A sv[15], const bool okB[15], int okN,
                        骨可见槽 *骨可见, bool isBot, float boxH) {
    if (okN < 2) return;    // 只要有两根可用就画（逐段判断，不再整具消失）

    const ImColor boneCol = IM_COL32(255, 60, 60, 255);
    const ImColor coverCol = isBot ? 人机掩体骨骼颜色 : 真人掩体骨骼颜色;
    float thick = std::max(0.5f, std::min(8.0f, 骨骼粗细));

    // 掩体配色统一走骨可见表（每 3 帧扫一轮），
    // 这里不再另打一套射线（省一半开销）。
    bool boneVis[15];
    for (int t = 0; t < 15; t++)
        boneVis[t] = 骨可见->就绪 ? 骨可见->态[t] : true;
    auto SegCol = [&](int a, int b) -> ImColor {
        bool va = 骨可见->就绪 ? 骨可见->态[a] : true;
        bool vb = 骨可见->就绪 ? 骨可见->态[b] : true;
        return (va && vb) ? boneCol : coverCol;
    };

    // 头部骨骼：头顶圆圈 + 头-胸连线
    if (okB[0])
        Draw->AddCircle({sv[0].X, sv[0].Y}, std::max(2.2f, boxH * 0.075f),
                        boneVis[0] ? boneCol : coverCol, 12, thick);
    auto L = [&](int a, int b) {
        if (!okB[a] || !okB[b]) return;   // 这根投不出来：只跳过这一段
        Draw->AddLine({sv[a].X, sv[a].Y}, {sv[b].X, sv[b].Y}, SegCol(a, b), thick);
    };
    L(1, 2);                       // 胸 - 骨盆
    L(1, 3); L(1, 4);              // 胸 - 双肩
    L(3, 5); L(5, 7);              // 左臂
    L(4, 6); L(6, 8);              // 右臂
    L(2, 9); L(9, 11); L(11, 13);  // 左腿
    L(2, 10); L(10, 12); L(12, 14); // 右腿
    // ---- 体型轮廓（跟骨骼一起画）：胸部 / 下部（蛋 / 柱 / 龟头）----
    {
        const float 肩半 = (okB[3] && okB[4]) ? fabsf(sv[4].X - sv[3].X) * 0.5f : 0.0f;
        const float 髋半 = (okB[9] && okB[10]) ? fabsf(sv[10].X - sv[9].X) * 0.5f : 0.0f;
        // 胸部：左右两个椭圆（以「胸 - 同侧肩」中点为心；饱满椭圆，不跟躯干拉长）
        if (okB[1] && okB[3] && okB[4] && 肩半 > 1.0f) {
            const float rx = 肩半 * 0.42f;
            const float ry = 肩半 * 0.62f;
            const ImColor 胸色 = (boneVis[1] && boneVis[3] && boneVis[4]) ? boneCol : coverCol;
            const float 左胸x = (sv[1].X + sv[3].X) * 0.5f, 左胸y = (sv[1].Y + sv[3].Y) * 0.5f + ry * 0.45f;
            const float 右胸x = (sv[1].X + sv[4].X) * 0.5f, 右胸y = (sv[1].Y + sv[4].Y) * 0.5f + ry * 0.45f;
            Draw->AddEllipse({左胸x, 左胸y}, {rx, ry}, 胸色, 0.0f, 24, thick);
            Draw->AddEllipse({右胸x, 右胸y}, {rx, ry}, 胸色, 0.0f, 24, thick);
            Draw->AddCircleFilled({左胸x, 左胸y + ry * 0.12f}, rx * 0.18f, 胸色);
            Draw->AddCircleFilled({右胸x, 右胸y + ry * 0.12f}, rx * 0.18f, 胸色);
        }
        // 下部：蛋 / 柱 / 龟头（骨盆 + 双大腿根定位；方向 = 骨盆 → 脚踝中点）
        if (okB[2] && okB[9] && okB[10] && okB[13] && okB[14]) {
            const float dx = (sv[13].X + sv[14].X) * 0.5f - sv[2].X;
            const float dy = (sv[13].Y + sv[14].Y) * 0.5f - sv[2].Y;
            const float 基准 = sqrtf(dx * dx + dy * dy);
            if (基准 > 8.0f) {
                const float ux = dx / 基准, uy = dy / 基准;    // 向下单位向量
                const float vx = -uy, vy = ux;                  // 横向单位向量
                const float 柱半 = std::max(1.6f, 髋半 * 0.32f);
                const float 柱长 = 基准 * 0.50f;
                const float 根推 = 柱半 * 0.6f;
                const float 根x = sv[2].X + ux * 根推, 根y = sv[2].Y + uy * 根推;
                const ImColor 色 = (boneVis[2] && boneVis[9] && boneVis[10]) ? boneCol : coverCol;
                const float 角 = atan2f(uy, ux);
                const float 蛋r = 柱半 * 1.25f;
                const float 蛋偏 = 柱半 * 1.70f;
                Draw->AddCircle({根x + vx * 蛋偏, 根y + vy * 蛋偏}, 蛋r, 色, 16, thick);
                Draw->AddCircle({根x - vx * 蛋偏, 根y - vy * 蛋偏}, 蛋r, 色, 16, thick);
                Draw->AddEllipse({根x + ux * 柱长 * 0.5f, 根y + uy * 柱长 * 0.5f},
                                 {柱长 * 0.5f, 柱半}, 色, 角, 24, thick);
                Draw->AddEllipse({根x + ux * 柱长, 根y + uy * 柱长},
                                 {柱半 * 1.05f, 柱半 * 0.72f}, 色, 角, 18, thick);
            }
        }
    }
}
