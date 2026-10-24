# 河流搜索程序

基于 cubiomes（外部依赖，不再随本仓库附带旧版 cubiomes）。

支持 Minecraft 1.18+ 的河流群系搜索；GUI 当前提供一个版本档：

| 版本档 | cubiomes 版本 |
| --- | --- |
| 1.18~26.3+ | `MC_26_3` |

## 构建

本仓库需要一份外部 cubiomes。默认布局：

```
<riverfinder>/../cubiomes
```

也就是 All-In-One Finder 工作区里的 `tools/cubiomes`。独立构建时用
`-DRIVER_CUBIOMES_DIR=<path-to-cubiomes>` 指定。仓库内自带的
`cubiomes/` 是旧版遗留目录，构建时不会使用。

```powershell
cmake -S . -B build -G Ninja -DRIVER_CUBIOMES_DIR=../cubiomes
cmake --build build
```

JNI 库：

```powershell
powershell -File build_jni_msys2.ps1
```

## 接口

- `riverSearch(...)`：旧入口，固定 `MC_1_21_3`，仅用于向后兼容。
- `riverSearchEx(seed, mc, ...)`：显式指定 `mc`，GUI 传 `MC_26_3`。
- JNI：`riverSearch` 保持旧签名；新增 `riverSearchEx`，在 seed 后多一个 `mc` 参数。

默认粗筛使用 `sampleRiverBiomeNoise()`（旧版 river-only 快速预筛的移植）。排查漏检时可加
`-DRIVER_STRICT_BIOME=1`，改成完整的 `sampleBiomeNoise()`。

粗筛使用流式调度：预先计算 chunk 总数，worker 用原子下标领取 chunk，不再为每个 chunk
创建任务对象；因此全图搜索的调度内存是 O(threads)，进度 `total` 是有效 chunk 数。

## 使用方法

编译后，依次输入种子、初始 x、初始 z，搜索半径 r（搜索区域为 2*r 的方形）。
