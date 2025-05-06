# DXRuby64

**DXRuby64** は 64ビット版の Ruby 3.1以降用にカスタムビルドされた **DXRuby** で、Universal C Runtime(UCRT)でビルドできるように[本家DXRuby](https://github.com/mirichi/dxruby)のソースにパッチを当てたものです。DXRuby64により、64ビット版の Rubyでも DirectX9をベースにした 2Dゲームを開発できるようになります。

**注意：**
この gem は個人によって作成された非公式バージョンであり、本家DXRubyのプロジェクトでメンテナンスされているものではありません。

[English README here](README.md)

## インストール

gem をインストールするには、以下のコマンドを実行してください。

```bash
gem install dxruby64
```

**注意：**
DXRuby64の動作に影響が出る可能性がありますので、[本家DXRuby](https://github.com/mirichi/dxruby) がインストールされていない状態で上記のコマンドを実行してください。
([本家DXRuby](https://github.com/mirichi/dxruby)は Ruby 3.1以降のバージョンには対応していないため、基本的に gemがインストールされていることはないと思われます)

## 使用方法
インストール後、従来どおり DXRubyの機能を使うことができます。

```ruby
require 'dxruby'

# 例：ウィンドウを作成し、単純な図形を表示
Window.width = 640
Window.height = 480

# 四角形を描画
Window.loop do
  Window.draw(100, 100, Image.new(50, 50, C_WHITE))
end
```

#### 【サンプルファイルについて】
この gem には sample/ ディレクトリ内に簡単なサンプルコードが含まれています。
(本家 DXRubyで提供されていたものをそのまま転載)

gem のインストール先は Ruby の環境によって異なりますが、以下のコマンドでその場所を確認することができます。

```
gem contents dxruby64
```

## 必要条件
- **Ruby 3.1 以降 (64ビット版)** が Windows にインストールされていること。

- `d3dx9_40.dll` が必要です。これは DirectX 9 ランタイムの一部です。もしインストールされていない場合は、[Microsoft の公式サイト](https://www.microsoft.com/en-us/download/details.aspx?id=8109)からダウンロードできます。

#### 【`d3dx9_40.dll` の入手方法】

1. [DirectX End-User Runtimes (June 2010)](https://www.microsoft.com/en-us/download/details.aspx?id=8109) をダウンロードします。

2. ダウンロードした `directx_Jun2010_redist.exe` をダブルクリックして、任意のディレクトリにファイルを展開します。

3. 展開したフォルダの中から `Nov2008_d3dx9_40_x64.cab` を探し、ダブルクリックします。

4. `Nov2008_d3dx9_40_x64.cab` の中にある `d3dx9_40.dll` を見つけ、Ruby のインストールフォルダ内の `bin` フォルダにコピー(または移動)します。（例：C:\Ruby34-x64\bin）

## Soundクラスの改修について

### 経緯など
[本家DXRuby](https://github.com/mirichi/dxruby) の Soundクラスは `DirectMusic` というライブラリを使用していましたが、
Windows 10/11 の 64bit 環境ではこの `DirectMusic` は正しく動作せず、以下のようなエラーが発生することが確認されています。

```
`DXRuby::Sound#initialize': DirectMusic initialize error - CoCreateInstance (DXRuby::DXRubyError)  
```
このため、DXRuby64では `DirectMusic` の代替として `DirectSound` というライブラリを使用するように改修をおこないました。
(`DirectSound` は SoundEffectクラスでもすでに使用されており、実績のあるライブラリです)

### 改修版 Soundクラスのサンプル

前述のサンプルファイルの中に `wav_sample/`ディレクトリに改修版の Soundクラスを用いたサンプルを追加しましたのでお試しください。
なお、下記の制限事項などについてもあわせてご確認ください。

### 制限事項など

`DirectSound` の機能上の制約などから、改修に伴って以下のような仕様変更をおこないました。

- Soundクラスで扱うことができるファイル形式は `.wav` のみ ([本家DXRuby](https://github.com/mirichi/dxruby) では `.mid` も可)

- 再生時のループ回数の指定はできず、「１回のみ再生」または「無限ループ」のいずれか

- ゲームなどで利用される効果音や BGMなどの再生を念頭に、以下に記載の最小限のメソッドのみを実装

```
■使用できるメソッド(新規にアレンジしたものも含む)

 .new             オブジェクトの生成
 #play            再生
 #stop            停止
 #volume=         音量設定(0～255)
 #set_volume      音量設定(上記 #volume= のエイリアス)  ※ただし従来の第2引数による音量フェード機能はなし
 #loop=           ループ再生の採否設定(下記 #loop_count と同じ機能で、trueまたは falseで設定するもの)
 #loop_count=     ループ回数設定(ただし、前述のように「１回のみ再生(0)」または「無限ループ(-1)」のみ可)
 #dispose         オブジェクトの破棄
 #disposed?       オブジェクト破棄状況のチェック
```

```
■使用できないメソッド(本家DXRubyでは実装されていたもの)

 .load_from_memory
 #start=
 #loop_start=
 #loop_end=
 #pan
 #pan=
 #frequency
 #frequency=
```

## ライセンス
この gem は zlib/libpngライセンスの下で提供されています。

## 免責事項
この gem は DXRubyの公式な後継ではなく、新しいバージョンの Ruby用に UCRT環境で動作することを目的としたカスタムビルドです。

## リポジトリ
ソースコードや詳細情報は、以下のリポジトリで確認できます。
https://github.com/spoolkitamura/dxruby64

<br>
<br>
<br>

## 開発者向けガイド

### `dxruby.so` のビルド手順

gemではビルド済みの .soファイルが提供されますが、開発者がソースから直接ビルドしたい場合は、以下の手順を参考にしてください。

#### 前提条件

- Ruby 3.1 以降 (64ビット版) がインストールされていること。

- `MSYS2` などの開発環境が用意されていること。
(Devkit付きの RubyInstallerを推奨)

#### 手順

1. リポジトリをクローンします。

```
git clone https://github.com/spoolkitamura/dxruby64.git
cd dxruby64
```

2. ext/dxruby64 フォルダに移動して、以下のコマンドで dxruby.so をビルドします。

```
cd ext/dxruby64
ridk enable
ruby extconf.rb       # Makefileを生成
make clean
make                  # dxruby.soを生成
```

ビルドされた dxruby.so ファイルは、`make install` を実行することによって Rubyの拡張ライブラリ用ディレクトリ(例: C:/Ruby31-x64/lib/ruby/site_ruby/3.1.0/x64-ucrt/)にインストールされます。
なお、このディレクトリの場所は、`RbConfig::CONFIG["sitearchdir"]` で確認することができます。

### 備考

- 本家 DXRubyのソースは ext/orgに置かれています。

- この gemでは、Rubyの各バージョンごとにビルドした dxruby.soを以下のディレクトリ構成で配置しています。

```
lib/
  dxruby.rb           ← バージョン振り分け処理
  31/
    dxruby.so         ← Ruby 3.1用
  32/
    dxruby.so         ← Ruby 3.2用
  33/
    dxruby.so         ← Ruby 3.3用
  34/
    dxruby.so         ← Ruby 3.4用
```

