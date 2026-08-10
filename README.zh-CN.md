# OneKVM NanoKVM OLED 扩展

[English](README.md) | 简体中文

这个扩展用来管理 NanoKVM PCIe 和 Cube 上自带的 OLED 小屏。装好以后，你可以直接在 OneKVM 页面里设计显示内容，查看 IP、分辨率、推流状态等信息，也可以设置屏幕方向、自动休眠和防烧屏。

它是一个独立扩展，不需要修改 OneKVM Core，可以随时启用、停用或卸载。

> **构建说明：** 本仓库只存放扩展源码。正式软件包由 `onekvm-distro` 中的 OpenEmbedded（OE）配方统一构建，本仓库暂不提供单独的发布包或一键构建流程。

## 支持的设备

扩展会根据 OneKVM 的设备信息和开机时的硬件探测结果，自动选择正确的屏幕，不需要手动填写 I2C 参数。

| 设备 | 屏幕配置 |
| --- | --- |
| NanoKVM PCIe | I2C 5，地址 `0x3c`，64×32 显示区域 |
| NanoKVM Cube Alpha | I2C 1，地址 `0x3d`，128×64 面板 |
| NanoKVM Cube Beta | I2C 5，地址 `0x3d`，128×64 面板 |
| NanoKVM Lite | 没有 OLED，不支持启用本扩展 |

如果设备是 NanoKVM Lite，插件管理器会阻止启用扩展，设置页面也会明确提示当前设备不支持。

## 可以做什么

- 在网页里拖拽设计 OLED 画面，添加文字、直线、矩形和自定义位图
- 像使用图像编辑器一样管理图层，包括预览、隐藏、排序、复制和粘贴
- 多选图层后统一居中、对齐或等距排列，并可拖动缩放文字、矩形和图片
- 显示 IP 地址、主机名、编码格式、FPS、分辨率、推流状态和运行时间
- 添加 ETH、HDMI、HID、Wi-Fi 状态图标，并分别设置连接和断开时的样式
- 显示直播观看状态，包括 `ON LIVE` 文字和闪烁指示点
- 混排 ASCII 与常用简体中文，支持 1×/2× 字号以及 3×5、4×5、5×5 英文字宽
- 设置 0°、90°、180°、270° 屏幕方向，网页预览会跟随实际显示方向
- 导入或导出完整的 JSON 布局，也可以一键恢复当前设备的默认布局
- 在长时间没有可见状态变化时自动息屏，并通过周期性移动一个像素来降低烧屏风险

恢复默认布局只会替换图层，不会改动屏幕开关、旋转方向、休眠时间或防烧屏设置。

## 安装与启用

软件包名称是 `onekvm-extension-nanokvm-oled`。请通过 OneKVM 发行版的软件包或镜像安装；安装完成后，可以用插件管理器启用或停用：

```sh
onekvm-plugin-manager enable nanokvm-oled
onekvm-plugin-manager disable nanokvm-oled
```

关闭扩展后，OLED 会先被清空并断电，后台显示进程也会退出，不会继续占用设备资源。

## 实现说明

- OLED 运行时使用 C++20 编写，通过 `sd-bus` 读取 `org.onekvm.Core1.Status` 的实时状态，并通过 `i2c-dev` 驱动屏幕。
- Web 设计器复用 OneKVM UI 提供的 Vue 和 Naive UI，不会重复打包一套前端框架。
- NanoKVM 的 OLED 没有实用的亮度调节能力，因此扩展使用控制器的固定对比度。
- 防烧屏位移只作用于真实屏幕，网页里的设计稿不会跟着移动。

网页预览和 C++ 渲染器使用同一份字形数据，因此两边的显示效果能够保持一致。字体来源、授权和重新生成方法统一放在 [`fonts/README.md`](fonts/README.md) 中。

## 开发时需要注意

OneKVM 会把扩展版本写入 Web 资源 URL，并把这些资源标记为不可变缓存。因此，只要修改了 `web/` 下的内容，就必须同步提升 manifest 和软件包版本。只增加 package revision，浏览器仍可能继续使用旧版画布代码。

安装锁定版本的 Web 依赖，然后构建并运行原生与浏览器测试：

```sh
npm ci
make test
```

原生构建需要支持 C++20 的编译器和 `libsystemd` 开发文件。`make test` 还会
先检查类型并打包 Vue 页面，再运行 C++ 和 Node.js 测试。

## 许可证

本项目使用 GNU General Public License v3.0，完整条款见 [`LICENSE`](LICENSE)。
