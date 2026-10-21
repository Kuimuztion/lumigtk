# Solfa

Solfa 是一个基于 GTK4 与 GStreamer 的本地音乐播放器，目标环境为 Fedora（Plasma 桌面）。
支持从剪贴板导入 B 站、抖音、快手的视频链接，下载后用 ffmpeg 转换为无损 FLAC 音频。

许可证：GPL-3.0-only，详见 [LICENSE](LICENSE)。

## 依赖

```bash
sudo dnf install gcc make pkgconf-pkg-config gtk4-devel gstreamer1-devel \
	gstreamer1-plugins-base gstreamer1-plugins-good yt-dlp curl
sudo dnf install ffmpeg        # 需要先启用 RPM Fusion (free)
```

## 编译与安装

```bash
make
sudo make install          # 安装二进制、桌面文件与图标，不触碰用户数据
solfa
sudo make uninstall        # 卸载
```

## 目录约定

- 视频临时文件：`/tmp/solfa/video/`
- 音频（flac）：`~/.config/solfa/audio/`，重新编译或安装不会覆盖或删除
- 设置：`~/.config/solfa/settings.ini`
- Cookie（可选）：`~/.config/solfa/cookies.txt`，Netscape 格式

如果你之前使用的是旧名称 fmusic，可以把旧数据目录迁移过来：

```bash
mv ~/.config/fmusic ~/.config/solfa
```

## 使用

1. 在任意程序中复制包含视频链接的文字（标题可以一起复制）。
2. 点击窗口右上角“导入”，程序从剪贴板中提取链接，下载并转换为音频。进度显示在底部进度条中。
3. 在列表中单击播放，右键重命名。正在播放的歌曲被重命名时播放会停止。
4. 底部可选择顺序播放、列表循环、单曲循环、随机播放，并可上一首、下一首、拖动进度条。拖动进度条时会静音，松开后从新位置继续播放。
5. 点击右下角“设置”：切换浅色 / 深色主题，选择图片作为壁纸（自动提取粉、绿等两到三色配色），调整面板不透明度，设置导入后是否自动播放。

## 图标

`data/dev.solfa.app.svg` 基于 `gnome-music.svg` 制作。原始图标的授权以其来源为准，发布前请确认其授权条款，必要时替换为自己的图标。
