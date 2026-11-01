# 河流搜索程序

基于 cubiomes 的河流群系搜索核心。cubiomes 作为本仓库的嵌套子模块放在
`cubiomes/`，因此本仓库可以独立构建，不再依赖 `../cubiomes`。

支持 Minecraft 1.18+ 的河流群系搜索；当前提供一个版本档：

| 版本档 | cubiomes 版本 |
| --- | --- |
| 1.18~26.3+ | `MC_26_3` |

## 子模块与构建

首次克隆后初始化嵌套子模块：

```powershell
git submodule update --init --recursive
```

### 独立 CLI

CMake 会同时构建静态引擎库和独立命令行程序：

```powershell
cmake -S . -B build -G Ninja
cmake --build build
.\build\bin\riverfinder-cli.exe --help
```

也可以不经过 CMake，直接用 MSYS2 UCRT64 构建单文件 exe：

```powershell
powershell -File build_cli_msys2.ps1
.\build\cli\riverfinder-cli.exe --help
```

生成的 `riverfinder-cli.exe` 在 MinGW 下静态链接 cubiomes 和 C++ 运行库，
可以直接复制到其他 Windows 机器运行，不需要 DLL 或外部源码树。

如果确实要复用另一份 cubiomes，可以覆盖默认路径：

```powershell
cmake -S . -B build -DRIVER_CUBIOMES_DIR=<path-to-cubiomes>
```

### JNI 库

```powershell
powershell -File build_jni_msys2.ps1
```

产物为 `build/native/libriverFinderLibJ.dll`。也可以使用 CMake：

```powershell
cmake -S jni -B build/jni -G Ninja
cmake --build build/jni
```

## 命令行用法

```powershell
# 使用默认搜索区域（以 0,0 为中心、边长 131072）和默认参数
riverfinder-cli --seed 12345

# 指定方形区域、采样高度、线程数，并导出 txt
riverfinder-cli --seed 12345 --square 131072 --y -64 --threads 8 --out river.txt

# 指定矩形区域和版本
riverfinder-cli --seed 12345 --mc 26.3 --min-x -10000 --max-x 10000 --min-z -10000 --max-z 10000
```

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--seed <int64>` | 必填 | 世界种子，可为负数 |
| `--mc <id\|version>` | `26.3` | 版本名（如 `26.3`、`1.21.3`、`latest`）或 cubiomes `MCVersion` 数值 |
| `--min-x` / `--max-x` | `-65536` / `65535` | 搜索区域 X 范围（含端点） |
| `--min-z` / `--max-z` | `-65536` / `65535` | 搜索区域 Z 范围（含端点） |
| `--square <side>` | 未设置 | 以 0,0 为中心的方形区域；不能与显式 min/max 混用 |
| `--min-area <int>` | `34763` | 最小河流面积 |
| `--y <int>` | `-64` | 群系采样高度 |
| `--threads <int>` | `min(32, CPU)` | 工作线程数，1~256 |
| `--cave-factor <float>` | `1.0` | 溶洞面积权重；`--no-cave` 等价于 `0` |
| `--limit <int>` | `1000` | 最多输出条数；`0` 表示不额外截断（引擎内部约 1000 条候选上限） |
| `--out <file>` | 未设置 | 同时把同一份文本报告写入文件 |
| `--help` / `-h` | — | 显示帮助 |
| `--version` | — | 显示 CLI 版本 |

输出为纯文本，默认打印到 stdout；指定 `--out` 时同时写入文件。格式如下：

```text
# RiverFinder CLI 1.0.0
# seed=12345 mc=26.3(35) search=[-65536,65535]x[-65536,65535] ... hits=1
x	z	river	cave	score	ratio(%)
123	456	34763	0	34763	70.03
```

`score = river + round(cave * caveFactor)`；`ratio` 使用与 Java GUI 相同的
满环面积 `49640` 计算。结果先按 `(x,z)` 去重，再按 64×64 方块单元保留最高分，
最后按分数、距原点距离、X、Z 排序。

## 接口

- `riverSearch(...)`：旧入口，固定 `MC_1_21_3`，仅用于向后兼容。
- `riverSearchEx(seed, mc, ...)`：显式指定 `mc`，GUI 传 `MC_26_3`。
- JNI：`riverSearch` 保持旧签名；`riverSearchEx` 在 seed 后多一个 `mc` 参数。
- CLI：使用 `riverSearchEx` 的 C ABI，并套用 GUI 的去重、64×64 分桶和排序规则；
  不另建一套搜索实现。

默认粗筛使用 `sampleRiverBiomeNoise()`（旧版 river-only 快速预筛的移植）。排查漏检时可加
`-DRIVER_STRICT_BIOME=1`，改成完整的 `sampleBiomeNoise()`。

粗筛使用流式调度：预先计算 chunk 总数，worker 用原子下标领取 chunk，不再为每个 chunk
创建任务对象；因此全图搜索的调度内存是 O(threads)，进度 `total` 是有效 chunk 数。
