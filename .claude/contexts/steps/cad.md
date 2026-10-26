# 几何导入

1. **没有 OCCT 时，平面解析器必须失败而不是假装成功。** 数据行不收在分号上，或出现圆、圆柱、圆锥、球、环面、B 样条、旋转面、偏置面，`importStep` 返回失败。单位先扫全文件：出现不带 milli 的米就乘 1000，否则按毫米。整场景十二个实体、T 型接头四块板仍能导入。没有「安装法兰过渡台」。
2. **机器人 STL 的米到毫米放在加载器，不放在通用 `importStl`。** `importRobotStlMillimeters` 读完再乘 1000。通用导入保持文件单位。
3. **装上 OCCT 之后先编通导入，再决定谁先解析。** `importStepWithOcct` 用 XCAF 读 STEP，边长用弧长积分，面法向用曲面性质。每块实体的名字来自 `MANIFOLD_SOLID_BREP`；这个名字在 CAF 传输后要用 `ShapeResult()` 取，`HasResult()` 会一直是假。实体名为空时，单实体文件退回产品名。这条路径里的索引图和标签序列已换成 OCCT 8 的 `NCollection` 类型。
4. **有 OCCT 时平面装配也走 OCCT。** 腹板 `Vertical_Web_Plate` 和前箱壁 `Workpiece_BoxWall_Front` 的边集合与内置解析相同，第 3 条焊缝的端点和方向也相同。面记录原点和包围盒，边记录相邻面，角焊缝的角平分线才能算出来。方向沿曲线参数。没有 OCCT，或 OCCT 读失败时，才退回内置解析。管法兰、U 型支架、方管、钢吊架已确认有非直线边和非平面。
