# XMF 专业工作台：第二轮实现记录

## 入口

```text
高级功能 → 专业工作台（欧加 / ROM / EDL）
```

或：

```text
XMF.exe --flash-mode pro
```

页面：`qml/pages/modes/ProWorkbenchPage.qml`

后端：`src/app/ProTools.cpp`、`src/flows/FlowTools.cpp`

工具总数：251

## 本轮新增

### GKI 制作

- `image.gki.unpack`：从 boot 提取 kernel，生成 `Image` + `gki.txt`
- `image.gki.pack`：把 GKI `Image` 换回 boot，保留原 ramdisk / cmdline

实测样本：

```text
C:\Users\shu\Desktop\s5pro\backup_9008_emmc\qcom_boot_20261125_123810.img
```

结果：

```text
Android boot v1
Image 11.5 MB
重打包后 magic = ANDROID!
```

### AVB 签名

- `image.avb.sign`：调用 AOSP `avbtool add_hash_footer`
- 支持算法选择、分区大小、私钥
- 未安装 avbtool 时明确报错，不生成假签名
- 另有 `image.vbmeta.info / disable / verify`

### 多线程分块下载

- `firmware.rom.multidownload`
- HTTP HEAD 检测 Content-Length / Accept-Ranges
- curl 分块并发（默认 4 线程）
- 不支持 Range 时回退单线程
- 合并后校验文件大小

实测：

```text
URL: raw.githubusercontent.com/xmz7891/XMF-flash-tool/main/README.md
线程: 2
SHA256 与单线程下载一致：
b4ba5f665e45815ab52d68df34bb9f2b5069dfa27256805786e74e98b89ffce7
```

### 强开 OEM 厂商策略

- `oem.unlock.strategy`
- 自动 / 小米 / 欧加 / 通用
- 依次尝试 `fastboot flashing unlock`、`fastboot oem unlock`
- 必须二次确认
- 仍受厂商解锁码、账号绑定、地区策略限制

### 欧加 / TWRP

已有工作台和工具：

- `oplus.package.inspect`
- `oplus.flash.plan`
- `oplus.flash.run`
- `oplus.ota.inspect`
- `oplus.ocdt.inspect / generate`
- `twrp.partitions / backup / restore / erase`

## 测试状态

已在本机验证：

- GKI 解包 / 重打包：通过
- 多线程下载：通过，SHA256 一致
- OCDT 模板生成：通过
- 欧加刷写计划生成：通过
- Release 构建：通过
- 页面 1500×900 / 980×620：通过

缺硬件，未做真机验证：

- 欧加全量包 / 售后包实际刷写
- TWRP Recovery 分区读写
- OEM 解锁命令（当前无 Fastboot 设备）

这两类不能标记为“实测通过”，需要对应设备接入后再跑。
